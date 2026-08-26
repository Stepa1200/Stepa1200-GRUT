// GRUT LinkManager hardware diagnostic firmware.
//
// Built only for env:esp8285-ground-linkdiag. This is intentionally NOT a
// bridge build: UART/COM is owned exclusively by this diagnostic output, so no
// ASCII is ever injected into a live MAVLink stream. Use it only for isolated
// LinkManager validation, then flash env:esp8285-ground back afterwards.

#include <Arduino.h>
#include <cstdio>

#include "FrameCodec.h"
#include "GrutProtocol.h"
#include "LinkManager.h"
#include "LinkStatsCodec.h"
#include "NeighborTable.h"
#include "RouteTable.h"
#include "grut/NodeConfig.h"
#include "grut/PhysicalUart.h"
#include "transport/EspNowDriver.h"
#include "transport/SequenceGenerator.h"

namespace {

constexpr uint8_t kEspNowChannel = 1;
constexpr uint32_t kPrintIntervalMs = 1000;

static_assert(grut::kOwnAddr == 2, "link diagnostic build is GROUND-only");

grut::transport::EspNowDriver gEspNow(kEspNowChannel, grut::kPeerMac);
grut::transport::SequenceGenerator gSequence;
grut::link::LinkManager gLinkManager;
grut::neighbor::NeighborTable gNeighborTable;
grut::routing::RouteTable gRouteTable;

grut::link::LinkState gLastPrintedState = grut::link::LinkState::kUnknown;
uint32_t gLastHeartbeatTxMs = 0;
uint32_t gLastPrintMs = 0;
uint32_t gObservedSendFailures = 0;
uint32_t gObservedQueueDrops = 0;
uint32_t gDecodeFailures = 0;

bool gHasPeerStats = false;
grut::link::LinkStats gPeerStats;
uint32_t gPeerStatsReceivedMs = 0;

const char* stateName(grut::link::LinkState state) {
  switch (state) {
    case grut::link::LinkState::kUnknown:
      return "UNKNOWN";
    case grut::link::LinkState::kUp:
      return "UP";
    case grut::link::LinkState::kDegraded:
      return "DEGRADED";
    case grut::link::LinkState::kDown:
      return "DOWN";
    case grut::link::LinkState::kRecovering:
      return "RECOVERING";
  }
  return "?";
}

void writeLine(const char* line) {
  Serial.print(line);
  Serial.print("\r\n");
}

uint32_t currentSendFailureCount() {
  return gEspNow.sendImmediateErrorCount() +
         gEspNow.sendCallbackFailureCount();
}

uint32_t currentQueueDropCount() {
  return gEspNow.droppedSendCount() + gEspNow.droppedReceiveCount();
}

void updateOperationalCounters() {
  const uint32_t sendFailures = currentSendFailureCount();
  const uint32_t sendFailureDelta = sendFailures - gObservedSendFailures;
  if (sendFailureDelta != 0u) {
    gLinkManager.onSendFailure(sendFailureDelta);
    gObservedSendFailures = sendFailures;
  }

  const uint32_t queueDrops = currentQueueDropCount();
  const uint32_t queueDropDelta = queueDrops - gObservedQueueDrops;
  if (queueDropDelta != 0u) {
    gLinkManager.onQueueDrop(queueDropDelta);
    gObservedQueueDrops = queueDrops;
  }
}

void sendHeartbeat() {
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat);
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::kPeerAddr;
  header.sequence = gSequence.next();

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, nullptr, 0, frame, sizeof(frame));
  if (frameLen > 0) {
    gEspNow.send(frame, frameLen);
  }
}

void processReceivedFrames(uint32_t nowMs) {
  uint8_t raw[grut::protocol::kMaxFrameSizeBytes];
  size_t rawLen = 0;
  uint8_t sourceMac[6];

  while (gEspNow.receive(raw, sizeof(raw), &rawLen, sourceMac)) {
    grut::protocol::GrutFrameHeader header;
    uint8_t payload[grut::protocol::kMaxGrutPayloadBytes];
    size_t payloadLen = 0;

    const grut::protocol::DecodeResult result = grut::protocol::decodeFrame(
        raw, rawLen, &header, payload, sizeof(payload), &payloadLen);
    if (result != grut::protocol::DecodeResult::kOk) {
      ++gDecodeFailures;
      continue;
    }

    // Stage 4.2 fix: NeighborTable (discovery) and LinkManager (link
    // health for the one fixed peer) have different scopes and must
    // not share one filter. A frame not addressed to us/broadcast is
    // never relevant to either and is dropped up front. Beyond that,
    // ANY valid srcAddr updates NeighborTable (that is the whole
    // point of discovery, e.g. AIR2 broadcasting HELLO); only frames
    // from grut::kPeerAddr are fed to gLinkManager, since LinkManager
    // models exactly one fixed peer relationship and has no meaning
    // for any other address. Previously this used a single combined
    // filter that rejected anything not from kPeerAddr BEFORE
    // gNeighborTable.onFrameObserved() ever ran - silently hiding
    // every other node from discovery, confirmed on real hardware
    // (AIR2's HELLO never appeared as a NEIGHBOR line despite AIR2
    // being powered and correctly flashed).
    if (header.dstAddr != grut::kOwnAddr && header.dstAddr != 0xFFu) {
      continue;
    }

    gNeighborTable.onFrameObserved(header.srcAddr, nowMs);
    gRouteTable.upsert(header.srcAddr, /*nextHop=*/header.srcAddr,
                       /*hopCount=*/1, nowMs);
    gEspNow.recordPeerBinding(header.srcAddr, sourceMac);

    if (header.srcAddr != grut::kPeerAddr) {
      continue;
    }

    gLinkManager.onFrameReceived(header.sequence, nowMs);

    if (header.type ==
        static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat)) {
      gLinkManager.onHeartbeat(nowMs);
      continue;
    }

    if (header.type ==
        static_cast<uint8_t>(grut::protocol::PacketType::kControl)) {
      grut::link::LinkStats decoded;
      if (grut::link::decodeLinkStats(payload, payloadLen, &decoded)) {
        gPeerStats = decoded;
        gHasPeerStats = true;
        gPeerStatsReceivedMs = nowMs;
      }
    }

    // kData is deliberately NOT forwarded to UART in this diagnostic build.
  }
}

void printTransition(uint32_t nowMs, grut::link::LinkState from,
                     grut::link::LinkState to) {
  char line[96];
  snprintf(line, sizeof(line), "TRANSITION t=%lu %s -> %s",
           static_cast<unsigned long>(nowMs), stateName(from), stateName(to));
  writeLine(line);
}

void printSnapshot(uint32_t nowMs) {
  const grut::link::LinkStats local = gLinkManager.snapshot(nowMs);
  char line[224];
  snprintf(
      line, sizeof(line),
      "LOCAL state=%s hbAge=%lu rx=%lu gaps=%lu loss10s=%u.%u%% "
      "lossLong=%u.%u%% sendFail=%lu qDrop=%lu decFail=%lu recHB=%u",
      stateName(local.state), static_cast<unsigned long>(local.heartbeatAgeMs),
      static_cast<unsigned long>(local.receivedFrames),
      static_cast<unsigned long>(local.sequenceGaps),
      static_cast<unsigned>(local.shortLossPermille / 10u),
      static_cast<unsigned>(local.shortLossPermille % 10u),
      static_cast<unsigned>(local.longLossPermille / 10u),
      static_cast<unsigned>(local.longLossPermille % 10u),
      static_cast<unsigned long>(local.sendFailures),
      static_cast<unsigned long>(local.queueDrops),
      static_cast<unsigned long>(gDecodeFailures),
      static_cast<unsigned>(local.recoveryHeartbeats));
  writeLine(line);

  if (gHasPeerStats) {
    const uint32_t peerAge = static_cast<uint32_t>(nowMs - gPeerStatsReceivedMs);
    snprintf(
        line, sizeof(line),
        "PEER state=%s statsAge=%lu hbAge=%lu rx=%lu gaps=%lu "
        "loss10s=%u.%u%% lossLong=%u.%u%% sendFail=%lu qDrop=%lu",
        stateName(gPeerStats.state), static_cast<unsigned long>(peerAge),
        static_cast<unsigned long>(gPeerStats.heartbeatAgeMs),
        static_cast<unsigned long>(gPeerStats.receivedFrames),
        static_cast<unsigned long>(gPeerStats.sequenceGaps),
        static_cast<unsigned>(gPeerStats.shortLossPermille / 10u),
        static_cast<unsigned>(gPeerStats.shortLossPermille % 10u),
        static_cast<unsigned>(gPeerStats.longLossPermille / 10u),
        static_cast<unsigned>(gPeerStats.longLossPermille % 10u),
        static_cast<unsigned long>(gPeerStats.sendFailures),
        static_cast<unsigned long>(gPeerStats.queueDrops));
    writeLine(line);
  } else {
    writeLine("PEER stats=not_seen");
  }
}

void printNeighbors(uint32_t nowMs) {
  const size_t total = gNeighborTable.count();
  if (total == 0) {
    writeLine("NEIGHBOR none");
    return;
  }

  for (size_t i = 0; i < total; ++i) {
    const grut::neighbor::NeighborInfo info = gNeighborTable.getByIndex(i);
    if (!info.known) {
      continue;  // shouldn't happen for i < count(), but never trust blindly
    }

    const uint32_t age = nowMs - info.lastSeenMs;
    const bool alive = gNeighborTable.isFresh(info.address, nowMs);

    // LinkState is not stored in NeighborTable (that would duplicate
    // LinkManager's job). This diagnostic firmware has exactly one
    // LinkManager instance, scoped to grut::kPeerAddr - state is only
    // meaningful for that one address; any other discovered neighbor
    // (e.g. AIR2) prints "n/a" rather than guessing at a health state
    // LinkManager never actually tracked for it.
    const char* state = (info.address == grut::kPeerAddr)
                             ? stateName(gLinkManager.state())
                             : "n/a";

    char line[112];
    snprintf(line, sizeof(line), "NEIGHBOR id=%u age=%lu alive=%u rx=%lu gaps=%lu state=%s",
             static_cast<unsigned>(info.address),
             static_cast<unsigned long>(age), alive ? 1 : 0,
             static_cast<unsigned long>(info.rxFrames),
             static_cast<unsigned long>(info.sequenceGaps), state);
    writeLine(line);
  }
}

void printPeerBindings(uint32_t nowMs) {
  const size_t total = gEspNow.peerBindingCount();
  if (total == 0) {
    writeLine("BINDING none");
  }
  for (size_t i = 0; i < total; ++i) {
    const grut::transport::EspNowDriver::BindingInfo info =
        gEspNow.getBindingByIndex(i);
    if (!info.known) {
      continue;
    }
    const uint32_t age = nowMs - info.lastSeenMs;
    char line[112];
    snprintf(line, sizeof(line),
             "BINDING addr=%u mac=%02X:%02X:%02X:%02X:%02X:%02X age=%lu",
             static_cast<unsigned>(info.grutAddr), info.mac[0], info.mac[1],
             info.mac[2], info.mac[3], info.mac[4], info.mac[5],
             static_cast<unsigned long>(age));
    writeLine(line);
  }

  char summary[112];
  snprintf(summary, sizeof(summary),
           "BINDING_STATS count=%lu dropped=%lu conflicts=%lu rebinds=%lu",
           static_cast<unsigned long>(total),
           static_cast<unsigned long>(gEspNow.droppedNewBindingCount()),
           static_cast<unsigned long>(gEspNow.addressMacConflictCount()),
           static_cast<unsigned long>(gEspNow.rebindCount()));
  writeLine(summary);
}

void printRoutes(uint32_t nowMs) {
  const size_t total = gRouteTable.count();
  if (total == 0) {
    writeLine("ROUTE none");
  }
  for (size_t i = 0; i < total; ++i) {
    const grut::routing::RouteEntry entry = gRouteTable.getByIndex(i);
    if (!entry.valid) {
      continue;
    }
    const uint32_t age = nowMs - entry.lastUpdatedMs;
    char line[112];
    snprintf(line, sizeof(line),
             "ROUTE dest=%u nextHop=%u hop=%u age=%lu",
             static_cast<unsigned>(entry.destination),
             static_cast<unsigned>(entry.nextHop),
             static_cast<unsigned>(entry.hopCount),
             static_cast<unsigned long>(age));
    writeLine(line);
  }
}

void printSendInFlightStats(uint32_t nowMs) {
  char line[176];
  snprintf(
      line, sizeof(line),
      "SENDINFLIGHT start=%lu cbOk=%lu cbFail=%lu cb=%lu curAge=%lu "
      "maxAge=%lu overThresh=%lu thresholdMs=%lu",
      static_cast<unsigned long>(gEspNow.sendAttemptedCount()),
      static_cast<unsigned long>(gEspNow.sendCallbackSuccessCount()),
      static_cast<unsigned long>(gEspNow.sendCallbackFailureCount()),
      static_cast<unsigned long>(gEspNow.sendCallbackSuccessCount() +
                                  gEspNow.sendCallbackFailureCount()),
      static_cast<unsigned long>(gEspNow.sendInFlightCurrentAgeMs(nowMs)),
      static_cast<unsigned long>(gEspNow.sendInFlightMaxAgeMs()),
      static_cast<unsigned long>(gEspNow.sendInFlightOverThresholdCount()),
      static_cast<unsigned long>(
          grut::transport::EspNowDriver::kSendInFlightObservationThresholdMs));
  writeLine(line);
}

}  // namespace

void setup() {
  Serial.begin(grut::kPhysicalUartBaud);
  delay(50);

  writeLine("");
  writeLine("GRUT GROUND LinkManager diagnostic");
  writeLine("DIAG ONLY: Mission Planner must be closed; DATA is not forwarded.");

  gLinkManager.reset();
  if (!gEspNow.start()) {
    writeLine("ESP_NOW start=FAILED");
    return;
  }

  gObservedSendFailures = currentSendFailureCount();
  gObservedQueueDrops = currentQueueDropCount();
  const uint32_t now = millis();
  gLastHeartbeatTxMs = now;
  gLastPrintMs = now;
  gLastPrintedState = gLinkManager.state();

  char line[80];
  snprintf(line, sizeof(line), "ESP_NOW start=OK channel=%u",
           static_cast<unsigned>(gEspNow.currentWifiChannel()));
  writeLine(line);
}

void loop() {
  gEspNow.poll();

  const uint32_t now = millis();
  processReceivedFrames(now);
  updateOperationalCounters();
  gLinkManager.poll(now);

  if (gLinkManager.state() != gLastPrintedState) {
    printTransition(now, gLastPrintedState, gLinkManager.state());
    gLastPrintedState = gLinkManager.state();
  }

  if (static_cast<uint32_t>(now - gLastHeartbeatTxMs) >=
      grut::link::LinkManager::kHeartbeatIntervalMs) {
    gLastHeartbeatTxMs = now;
    sendHeartbeat();
  }

  if (static_cast<uint32_t>(now - gLastPrintMs) >= kPrintIntervalMs) {
    gLastPrintMs = now;
    printSnapshot(now);
    printNeighbors(now);
    printPeerBindings(now);
    printRoutes(now);
    printSendInFlightStats(now);
  }
}
