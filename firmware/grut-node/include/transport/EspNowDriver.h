#pragma once

#include <cstddef>
#include <cstdint>

#include "FrameQueue.h"

namespace grut {
namespace transport {

// ESP-NOW driver (ADR 0005, ADR 0006).
//
// Moves raw byte buffers between this node and a single, fixed peer
// over ESP-NOW. This driver does not parse frame contents - encoding/
// decoding is FrameCodec's job (lib/grut_protocol), kept separate per
// ADR 0001 ("Transport must not contain BIOS logic" / clean layering).
//
// Wi-Fi mode/channel setup, peer registration, and the two ESP-NOW
// callbacks are owned here. FrameQueue (host-testable, no Arduino
// dependency) provides the bounded, drop-newest-on-overflow buffering
// ADR 0006 requires between the ESP-NOW callbacks - which run in the
// Wi-Fi task and must stay minimal, per Espressif's own guidance - and
// the main loop.
//
// Only one EspNowDriver instance may be active (started) at a time:
// the ESP-NOW callbacks are plain C function pointers with no
// user-data parameter, so they reach the active instance through a
// single static pointer (see EspNowDriver.cpp). This matches "exactly
// two nodes, one driver per node" from ADR 0005.
class EspNowDriver {
 public:
  // channel: shared Wi-Fi channel, fixed for both peers (ADR 0006).
  // peerMac: 6-byte MAC address of the single peer this node talks to.
  EspNowDriver(uint8_t channel, const uint8_t peerMac[6]);

  // Sets Wi-Fi mode/channel, initializes ESP-NOW, registers callbacks
  // and the single peer. Returns false if any step fails; in that case
  // isRunning() is false and no callback is left registered.
  bool start();

  // Unregisters callbacks, removes the peer, deinitializes ESP-NOW.
  // Idempotent.
  void stop();

  bool isRunning() const;

  // Diagnostic only: the channel actually applied by the SDK right
  // now (wifi_get_channel()), as opposed to the channel this driver
  // was constructed with. ESP8266's wifi_set_channel() is known to be
  // unreliable in some conditions - compare this against the
  // constructor's `channel` argument to confirm they actually match.
  uint8_t currentWifiChannel() const;

  // Feeds the next queued outgoing frame to esp_now_send() if none is
  // currently in flight. Call every loop() iteration.
  void poll();

  // Enqueues a raw frame for transmission. Returns false (and
  // increments droppedSendCount()) if the send queue is full or the
  // driver is not running. frameLength must not exceed
  // FrameQueue::kMaxFrameBytes.
  bool send(const uint8_t* frameBytes, size_t frameLength);

  // Lowest-priority/best-effort management send. The frame is accepted only
  // when there is no ESP-NOW frame in flight and the normal DATA send queue
  // is empty. Returning false here does NOT increment droppedSendCount():
  // management telemetry is allowed to be skipped and must never consume
  // capacity needed by the transported UART byte stream.
  bool sendIfIdle(const uint8_t* frameBytes, size_t frameLength);

  // True only when there is no frame currently in flight and the normal
  // send queue is empty. Intended for management/diagnostic scheduling.
  bool txIdle() const;

  // Pops one received raw frame, if any. Returns false if the receive
  // queue is empty.
  bool receive(uint8_t* outBuffer, size_t outCapacity, size_t* outLength);

  uint32_t droppedSendCount() const;
  uint32_t droppedReceiveCount() const;

  // Diagnostic counters, all monotonically increasing from start().
  // sendAttempted: every time esp_now_send() was actually called.
  // sendImmediateErrorCount: esp_now_send() itself returned non-zero -
  //   in this case sendInFlight_ is cleared right away (see
  //   trySendNext()) so the driver never gets stuck waiting for a
  //   callback that was never going to fire.
  // sendCallbackSuccessCount / sendCallbackFailureCount: tallied from
  //   the send callback's `status` parameter, which earlier versions
  //   of this driver ignored entirely.
  uint32_t sendAttemptedCount() const;
  uint32_t sendImmediateErrorCount() const;
  uint32_t sendCallbackSuccessCount() const;
  uint32_t sendCallbackFailureCount() const;

 private:
  static void onSend(uint8_t* macAddr, uint8_t status);
  static void onReceive(uint8_t* macAddr, uint8_t* data, uint8_t length);

  void trySendNext();

  uint8_t channel_;
  uint8_t peerMac_[6];
  bool running_ = false;
  bool sendInFlight_ = false;

  FrameQueue sendQueue_;
  FrameQueue recvQueue_;

  uint32_t sendAttempted_ = 0;
  uint32_t sendImmediateErrors_ = 0;
  uint32_t sendCallbackSuccesses_ = 0;
  uint32_t sendCallbackFailures_ = 0;
};

}  // namespace transport
}  // namespace grut
