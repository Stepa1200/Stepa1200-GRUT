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

  while (gEspNow.receive(raw, sizeof(raw), &rawLen)) {
    grut::protocol::GrutFrameHeader header;
    uint8_t payload[grut::protocol::kMaxGrutPayloadBytes];
    size_t payloadLen = 0;

    const grut::protocol::DecodeResult result = grut::protocol::decodeFrame(
        raw, rawLen, &header, payload, sizeof(payload), &payloadLen);
    if (result != grut::protocol::DecodeResult::kOk) {
      ++gDecodeFailures;
      continue;
    }

    // This diagnostic has one fixed ESP-NOW peer. Still ignore a valid GRUT
    // frame whose protocol addresses do not target this node/broadcast.
    if (header.srcAddr != grut::kPeerAddr ||
        (header.dstAddr != grut::kOwnAddr && header.dstAddr != 0xFFu)) {
      continue;
    }

    gLinkManager.onFrameReceived(header.sequence, nowMs);
    gNeighborTable.onFrameObserved(header.srcAddr, nowMs);

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
  // Today's topology is fixed 1-to-1, so the only address that can ever
  // appear in NeighborTable is grut::kPeerAddr (a compile-time
  // constant already used elsewhere in this file - see
  // processReceivedFrames()). This means the existing get(address) API
  // is sufficient here; NeighborTable itself needs no changes and no
  // enumeration method. Revisit this once a real multi-neighbor
  // topology exists.
  const grut::neighbor::NeighborInfo info = gNeighborTable.get(grut::kPeerAddr);
  if (!info.known) {
    writeLine("NEIGHBOR none");
    return;
  }

  const uint32_t age = nowMs - info.lastSeenMs;
  const bool alive = gNeighborTable.isFresh(info.address, nowMs);

  // LinkState is not stored in NeighborTable (that would duplicate
  // LinkManager's job - see NeighborTable.h). This diagnostic firmware
  // has exactly one LinkManager instance for this single fixed peer,
  // so it is safe to report gLinkManager's state alongside this entry.
  char line[112];
  snprintf(line, sizeof(line), "NEIGHBOR id=%u age=%lu alive=%u rx=%lu gaps=%lu state=%s",
           static_cast<unsigned>(info.address),
           static_cast<unsigned long>(age), alive ? 1 : 0,
           static_cast<unsigned long>(info.rxFrames),
           static_cast<unsigned long>(info.sequenceGaps),
           stateName(gLinkManager.state()));
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
  }
}
