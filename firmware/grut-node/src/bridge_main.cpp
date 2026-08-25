// GRUT bridge firmware entry point (v0.2.0, ADR 0005).
//
// Built only for env:esp8285-air / env:esp8285-ground - excluded from
// the BIOS-only env:esp8285 build. No BIOS, no console: this firmware
// is pure UART <-> ESP-NOW transport. AIR and GROUND run this exact
// same file; only the role macros baked into NodeConfig.h (via
// per-environment build_flags) differ between the two.

#include <Arduino.h>

#include "FrameCodec.h"
#include "GrutProtocol.h"
#include "HelloCodec.h"
#include "LinkManager.h"
#include "LinkStatsCodec.h"
#include "NeighborTable.h"
#include "grut/NodeConfig.h"
#include "transport/EspNowDriver.h"
#include "transport/FrameBuilder.h"
#include "transport/FrameReceiver.h"
#include "transport/SequenceGenerator.h"
#include "transport/UartTransport.h"

namespace {

// Shared Wi-Fi channel for both peers (ADR 0006) - both nodes must be
// built with the same value.
constexpr uint8_t kEspNowChannel = 1;

// TEMPORARY bring-up aid: send an empty kHeartbeat frame on this
// interval, independent of any real UART traffic, so the ESP-NOW link
// itself can be proven alive without a flight controller attached.
constexpr unsigned long kHeartbeatIntervalMs =
    grut::link::LinkManager::kHeartbeatIntervalMs;

// Permanent LinkManager v1 statistics export interval (ADR 0007). The
// payload is a formal LINK_STATS kControl message encoded by LinkStatsCodec.
// It never reaches the MAVLink UART; FrameReceiver consumes kControl frames
// internally and exposes them only through the control observer.
constexpr unsigned long kLinkStatsIntervalMs = 3000;
// LINK_STATS is management telemetry and must never compete with active UART
// transport. Require a short local-UART quiet window before a best-effort send.
constexpr unsigned long kLinkStatsUartQuietMs = 500;

// Stage 4.2 (neighbor discovery): periodic broadcast HELLO interval.
// Deliberately much less frequent than heartbeat (1s) - discovery does
// not need to be instant, and every extra periodic broadcast is more
// contention for the single shared ESP-NOW send slot (see the
// LINK_STATS/parameter-load regression investigation). 2000ms is a
// first, provisional choice, not derived from a specific measurement -
// open to tuning once multi-node hardware testing exists. HELLO reuses
// kLinkStatsUartQuietMs's exact gating (quiet UART + txIdle) for the
// same reason LINK_STATS does.
constexpr unsigned long kHelloIntervalMs = 2000;

// Master switch for ALL diagnostic UART writes (both the received-stats
// print in FrameReceiver and printLocalStats() below). OFF by default:
// once real UART traffic (MAVLink or otherwise) is flowing, nothing may
// write unsolicited bytes into that same stream - see FrameReceiver.h.
// Flip to true only for a dedicated bring-up/diagnosis session with no
// real UART traffic expected to be parsed downstream.
constexpr bool kDiagnosticsEnabled = false;

grut::transport::UartTransport gUart;
grut::transport::EspNowDriver gEspNow(kEspNowChannel, grut::kPeerMac);
grut::transport::SequenceGenerator gSequence;
grut::transport::FrameBuilder gFrameBuilder(gUart, gEspNow, gSequence,
                                             grut::kOwnAddr, grut::kPeerAddr);
grut::link::LinkManager gLinkManager;
grut::neighbor::NeighborTable gNeighborTable;

void observeValidFrame(uint8_t srcAddr, uint16_t sequence, uint8_t packetType);
void observeControlFrame(uint8_t srcAddr, const uint8_t* payload,
                         size_t payloadLength);

// Stage 4.2 safety gate: the ONLY srcAddr whose kData frames may reach
// this node's UART. Deliberately reuses grut::kPeerAddr - the same
// static, configured peer identity already used for HEARTBEAT/DATA/
// LINK_STATS unicast - rather than introducing a second, separate
// "active peer" identity source. NeighborTable (fed by HELLO and any
// other valid frame, from any address) is not consulted here at all:
// being discovered is not the same as being authorized to write UART.
bool acceptDataSource(uint8_t srcAddr) {
  return srcAddr == grut::kPeerAddr;
}

// TEMPORARY: turn both back to false once done - see FrameReceiver.h
// for why writing unsolicited bytes to UART is unsafe once real
// MAVLink/data traffic is flowing on this same line. Both are now
// OFF: the transport itself was proven working via the byte-pattern
// test (see bring-up notes) - the STATS/LOCAL ASCII lines must not
// stay on while real MAVLink is flowing, or they corrupt it exactly
// like the earlier heartbeat marker did.
grut::transport::FrameReceiver gFrameReceiver(
    gEspNow, gUart,
    /*debugPrintHeartbeats=*/false,
    /*debugPrintStats=*/kDiagnosticsEnabled,
    /*validFrameObserver=*/&observeValidFrame,
    /*controlFrameObserver=*/&observeControlFrame,
    /*dataSourceFilter=*/&acceptDataSource);

unsigned long gLastHeartbeatMs = 0;
unsigned long gLastLinkStatsMs = 0;
unsigned long gLastHelloMs = 0;
unsigned long gLastUartActivityMs = 0;
uint32_t gLastObservedUartBytesRead = 0;
uint32_t gObservedSendFailures = 0;
uint32_t gObservedQueueDrops = 0;

// Latest peer-exported LinkManager snapshot. This is deliberately kept in
// memory only for now: Desktop/management integration comes later in the
// roadmap, and the MAVLink UART must remain an opaque byte stream.
grut::link::LinkStats gPeerLinkStats;
bool gHasPeerLinkStats = false;
uint32_t gPeerLinkStatsReceivedMs = 0;

uint32_t currentSendFailureCount() {
  return gEspNow.sendImmediateErrorCount() +
         gEspNow.sendCallbackFailureCount();
}

uint32_t currentQueueDropCount() {
  return gEspNow.droppedSendCount() + gEspNow.droppedReceiveCount();
}

void observeValidFrame(uint8_t srcAddr, uint16_t sequence, uint8_t packetType) {
  const uint32_t now = millis();
  gLinkManager.onFrameReceived(sequence, now);
  if (packetType ==
      static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat)) {
    gLinkManager.onHeartbeat(now);
  }
  gNeighborTable.onFrameObserved(srcAddr, now);
}

void observeControlFrame(uint8_t srcAddr, const uint8_t* payload,
                         size_t payloadLength) {
  // Stage 4.2: kControl now carries two subtypes (see HelloCodec.h /
  // LinkStatsCodec.h). NeighborTable itself is already updated for
  // HELLO frames via the generic observeValidFrame() path above (it
  // updates for every valid GRUT frame, any type) - this function's
  // only remaining job is subtype-specific payload handling.
  if (payloadLength == 0) {
    return;
  }

  if (grut::discovery::decodeHello(payload, payloadLength)) {
    // Discovery only - identity (srcAddr) already recorded by
    // observeValidFrame(). Nothing further to do; HELLO carries no
    // other fields by design.
    return;
  }

  if (srcAddr != grut::kPeerAddr) {
    // LINK_STATS is only meaningful from this node's own fixed link
    // peer (gPeerLinkStats models exactly one remote LinkManager
    // snapshot) - a HELLO from any other node was already handled
    // above and returned; anything else from a non-peer address is
    // ignored here rather than guessed at.
    return;
  }

  grut::link::LinkStats decoded;
  if (grut::link::decodeLinkStats(payload, payloadLength, &decoded)) {
    gPeerLinkStats = decoded;
    gHasPeerLinkStats = true;
    gPeerLinkStatsReceivedMs = millis();
  }
}

void updateLinkManager(uint32_t nowMs) {
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

  gLinkManager.poll(nowMs);
}

void sendHeartbeat() {
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat);
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::kPeerAddr;
  header.sequence = gSequence.next();

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, nullptr, 0, frameBuf, sizeof(frameBuf));
  if (frameLen > 0) {
    gEspNow.send(frameBuf, frameLen);
  }
}

void sendLinkStats(uint32_t nowMs) {
  uint8_t payload[grut::link::kLinkStatsPayloadSize];
  const grut::link::LinkStats snapshot = gLinkManager.snapshot(nowMs);
  const size_t payloadLen =
      grut::link::encodeLinkStats(snapshot, payload, sizeof(payload));
  if (payloadLen == 0) {
    return;
  }

  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kControl);
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::kPeerAddr;
  header.sequence = gSequence.next();

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, payload, payloadLen, frameBuf, sizeof(frameBuf));
  if (frameLen > 0) {
    // LINK_STATS is strictly lower priority than transported DATA. If the
    // radio is busy, skip this snapshot rather than consuming DATA queue
    // capacity or extending a MAVLink transaction.
    gEspNow.sendIfIdle(frameBuf, frameLen);
  }
}

void sendHello() {
  uint8_t payload[grut::discovery::kHelloPayloadSize];
  const size_t payloadLen =
      grut::discovery::encodeHello(payload, sizeof(payload));
  if (payloadLen == 0) {
    return;
  }

  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kControl);
  header.flags = grut::protocol::kFlagBroadcast;
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::protocol::kBroadcastAddress;
  header.sequence = gSequence.next();

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, payload, payloadLen, frameBuf, sizeof(frameBuf));
  if (frameLen > 0) {
    // Same "skip rather than contend" philosophy as LINK_STATS -
    // discovery must never compete with transported DATA.
    gEspNow.sendBroadcastIfIdle(frameBuf, frameLen);
  }
}

// TEMPORARY diagnostic aid: print THIS node's own counters directly to
// its own UART - no ESP-NOW round trip, no dependence on the (observed
// to be unreliable on ESP8266) send-callback status. This is the
// ground truth for what THIS node's own EspNowDriver/FrameReceiver
// actually saw, independent of what the other node's callback claims
// about delivery. Prefixed "LOCAL " to distinguish from "STATS " lines
// relayed from the peer (see FrameReceiver's debugPrintStats).
void printLocalStats() {
  char line[192];
  const int len = snprintf(
      line, sizeof(line),
      "LOCAL src=%u rd=%lu tx=%lu txAttempt=%lu txErr=%lu txOk=%lu "
      "txFail=%lu txDrop=%lu rxDrop=%lu dec=%lu gap=%lu wr=%lu wrFail=%lu\r\n",
      static_cast<unsigned>(grut::kOwnAddr),
      static_cast<unsigned long>(gFrameBuilder.uartBytesRead()),
      static_cast<unsigned long>(gFrameBuilder.framesSent()),
      static_cast<unsigned long>(gEspNow.sendAttemptedCount()),
      static_cast<unsigned long>(gEspNow.sendImmediateErrorCount()),
      static_cast<unsigned long>(gEspNow.sendCallbackSuccessCount()),
      static_cast<unsigned long>(gEspNow.sendCallbackFailureCount()),
      static_cast<unsigned long>(gEspNow.droppedSendCount()),
      static_cast<unsigned long>(gEspNow.droppedReceiveCount()),
      static_cast<unsigned long>(gFrameReceiver.decodeFailureCount()),
      static_cast<unsigned long>(gFrameReceiver.sequenceGapCount()),
      static_cast<unsigned long>(gFrameReceiver.uartBytesWritten()),
      static_cast<unsigned long>(gFrameReceiver.uartWriteFailureCount()));
  if (len > 0) {
    gUart.send(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
  }
}

}  // namespace

void setup() {
  gUart.start();
  gEspNow.start();
  gLinkManager.reset();
  gObservedSendFailures = currentSendFailureCount();
  gObservedQueueDrops = currentQueueDropCount();
  gLastHeartbeatMs = millis();
  gLastLinkStatsMs = millis();
  gLastHelloMs = millis();
  gLastUartActivityMs = millis();
  gLastObservedUartBytesRead = gFrameBuilder.uartBytesRead();
}

void loop() {
  gEspNow.poll();
  gFrameBuilder.poll();
  gFrameReceiver.poll();

  const unsigned long now = millis();
  const uint32_t uartBytesRead = gFrameBuilder.uartBytesRead();
  if (uartBytesRead != gLastObservedUartBytesRead) {
    gLastObservedUartBytesRead = uartBytesRead;
    gLastUartActivityMs = now;
  }

  if (now - gLastHeartbeatMs >= kHeartbeatIntervalMs) {
    gLastHeartbeatMs = now;
    sendHeartbeat();
  }
  // Update the passive health model before exporting its snapshot so the
  // periodic LINK_STATS frame reflects all counters observed this loop.
  // Still no recovery action is allowed in Step 2.1c.
  updateLinkManager(static_cast<uint32_t>(now));

  if (now - gLastLinkStatsMs >= kLinkStatsIntervalMs &&
      now - gLastUartActivityMs >= kLinkStatsUartQuietMs &&
      gEspNow.txIdle()) {
    gLastLinkStatsMs = now;
    sendLinkStats(static_cast<uint32_t>(now));
    if (kDiagnosticsEnabled) {
      printLocalStats();
    }
  }

  if (now - gLastHelloMs >= kHelloIntervalMs &&
      now - gLastUartActivityMs >= kLinkStatsUartQuietMs &&
      gEspNow.txIdle()) {
    gLastHelloMs = now;
    sendHello();
  }
}
