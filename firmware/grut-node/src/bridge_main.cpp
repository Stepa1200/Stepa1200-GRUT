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
#include "grut/NodeConfig.h"
#include "transport/EspNowDriver.h"
#include "transport/FrameBuilder.h"
#include "transport/FrameReceiver.h"
#include "transport/UartTransport.h"

namespace {

// Shared Wi-Fi channel for both peers (ADR 0006) - both nodes must be
// built with the same value.
constexpr uint8_t kEspNowChannel = 1;

// TEMPORARY bring-up aid: send an empty kHeartbeat frame on this
// interval, independent of any real UART traffic, so the ESP-NOW link
// itself can be proven alive without a flight controller attached.
constexpr unsigned long kHeartbeatIntervalMs = 1000;

// TEMPORARY diagnostic aid: send this node's own counters (FrameBuilder
// + EspNowDriver + FrameReceiver) as a kControl frame on this interval,
// so the OTHER node's UART (the one being observed) can print them -
// see FrameReceiver's debugPrintStats. Not part of the GRUT protocol
// spec; ad hoc wire format shared only between sendStats() below and
// FrameReceiver.cpp's matching parser.
constexpr unsigned long kStatsIntervalMs = 3000;

// Master switch for ALL diagnostic UART writes (both the received-stats
// print in FrameReceiver and printLocalStats() below). OFF by default:
// once real UART traffic (MAVLink or otherwise) is flowing, nothing may
// write unsolicited bytes into that same stream - see FrameReceiver.h.
// Flip to true only for a dedicated bring-up/diagnosis session with no
// real UART traffic expected to be parsed downstream.
constexpr bool kDiagnosticsEnabled = false;

grut::transport::UartTransport gUart;
grut::transport::EspNowDriver gEspNow(kEspNowChannel, grut::kPeerMac);
grut::transport::FrameBuilder gFrameBuilder(gUart, gEspNow, grut::kOwnAddr,
                                             grut::kPeerAddr);
// TEMPORARY: turn both back to false once done - see FrameReceiver.h
// for why writing unsolicited bytes to UART is unsafe once real
// MAVLink/data traffic is flowing on this same line. Both are now
// OFF: the transport itself was proven working via the byte-pattern
// test (see bring-up notes) - the STATS/LOCAL ASCII lines must not
// stay on while real MAVLink is flowing, or they corrupt it exactly
// like the earlier heartbeat marker did.
grut::transport::FrameReceiver gFrameReceiver(gEspNow, gUart,
                                               /*debugPrintHeartbeats=*/false,
                                               /*debugPrintStats=*/kDiagnosticsEnabled);

unsigned long gLastHeartbeatMs = 0;
unsigned long gLastStatsMs = 0;

void sendHeartbeat() {
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat);
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::kPeerAddr;

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, nullptr, 0, frameBuf, sizeof(frameBuf));
  if (frameLen > 0) {
    gEspNow.send(frameBuf, frameLen);
  }
}

void putU32LE(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFF);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Wire format: 12 uint32 fields, little-endian, in this exact order.
// Must match FrameReceiver.cpp's parser exactly - see its comment.
void sendStats() {
  uint8_t payload[48];
  size_t off = 0;
  auto put = [&](uint32_t v) {
    putU32LE(payload + off, v);
    off += 4;
  };

  put(gFrameBuilder.uartBytesRead());
  put(gFrameBuilder.framesSent());
  put(gEspNow.sendAttemptedCount());
  put(gEspNow.sendImmediateErrorCount());
  put(gEspNow.sendCallbackSuccessCount());
  put(gEspNow.sendCallbackFailureCount());
  put(gEspNow.droppedSendCount());
  put(gEspNow.droppedReceiveCount());
  put(gFrameReceiver.decodeFailureCount());
  put(gFrameReceiver.sequenceGapCount());
  put(gFrameReceiver.uartBytesWritten());
  put(gFrameReceiver.uartWriteFailureCount());

  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kControl);
  header.srcAddr = grut::kOwnAddr;
  header.dstAddr = grut::kPeerAddr;

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen =
      grut::protocol::encodeFrame(header, payload, off, frameBuf, sizeof(frameBuf));
  if (frameLen > 0) {
    gEspNow.send(frameBuf, frameLen);
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
  gLastHeartbeatMs = millis();
  gLastStatsMs = millis();
}

void loop() {
  gEspNow.poll();
  gFrameBuilder.poll();
  gFrameReceiver.poll();

  const unsigned long now = millis();
  if (now - gLastHeartbeatMs >= kHeartbeatIntervalMs) {
    gLastHeartbeatMs = now;
    sendHeartbeat();
  }
  if (kDiagnosticsEnabled && now - gLastStatsMs >= kStatsIntervalMs) {
    gLastStatsMs = now;
    sendStats();
    printLocalStats();
  }
}