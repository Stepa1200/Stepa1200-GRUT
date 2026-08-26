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
  // Stage 4.2 (neighbor discovery): the broadcast MAC, also registered
  // as a peer in start() so sendBroadcastIfIdle() can target it.
  // Registering the broadcast peer does not change this driver's
  // primary fixed-peer relationship (send()/sendIfIdle()/poll() all
  // still target the constructor's `peerMac` exactly as before) - it
  // only enables the separate, low-frequency broadcast path used for
  // discovery.
  static constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF};
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

  // Stage 4.2: same "skip rather than queue/retry" philosophy as
  // sendIfIdle(), but targets kBroadcastMac instead of the fixed
  // peer - used only for periodic HELLO discovery frames. Bypasses
  // sendQueue_ entirely (no retry, no backpressure) since a missed
  // HELLO simply means discovery takes one cycle longer, never a
  // correctness problem.
  bool sendBroadcastIfIdle(const uint8_t* frameBytes, size_t frameLength);

  // True only when there is no frame currently in flight and the normal
  // send queue is empty. Intended for management/diagnostic scheduling.
  bool txIdle() const;

  // Pops one received raw frame, if any. Returns false if the receive
  // queue is empty.
  //
  // outMac: optional, 6 bytes. Filled with the ESP-NOW source MAC of
  // this frame if provided (Stage 5.0). nullptr means "caller doesn't
  // need it" - existing callers are unaffected.
  bool receive(uint8_t* outBuffer, size_t outCapacity, size_t* outLength,
               uint8_t* outMac = nullptr);

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

  // --- sendInFlight_ stall investigation instrumentation (observation
  // only - does not change scheduling or recovery behavior). ---
  //
  // Provisional threshold for counting only, not for any corrective
  // action. Justified from real ESP8266-specific ESP-NOW measurements
  // (independent sources report ~7-11ms typical send->callback RTT on
  // this exact chip) - 100ms is >10x that, so exceeding it indicates a
  // genuine stall, not ordinary jitter.
  static constexpr uint32_t kSendInFlightObservationThresholdMs = 100;

  // 0 if nothing is currently in flight; otherwise how long the
  // current send has been waiting for its callback, as of `nowMs`.
  uint32_t sendInFlightCurrentAgeMs(uint32_t nowMs) const;

  // Longest observed duration between a send starting and
  // sendInFlight_ clearing (by any path - immediate error or async
  // callback), since start().
  uint32_t sendInFlightMaxAgeMs() const;

  // How many completed sends took longer than
  // kSendInFlightObservationThresholdMs to clear.
  uint32_t sendInFlightOverThresholdCount() const;

  // --- Stage 5.0: GRUT address -> ESP-NOW MAC endpoint plumbing. ---
  //
  // Transport (this class), not NeighborTable, owns this mapping -
  // NeighborTable stays carrier-independent (see its header comment).
  // FrameReceiver calls recordPeerBinding() once per successfully
  // decoded frame, immediately after learning both the source MAC
  // (from receive()'s outMac) and the GRUT address (from the decoded
  // header) - this class never decodes GRUT frames itself.
  //
  // This is NOT authentication or security - it is deterministic
  // endpoint ownership ahead of a future SecurityManager. A GRUT
  // address is not allowed to be silently hijacked by a second MAC
  // while its current binding is still fresh.
  static constexpr size_t kMaxPeerBindings = 8;

  // Same staleness horizon as NeighborTable::kDefaultStaleAfterMs -
  // both answer "how long since we last heard from this address
  // before we stop trusting what we knew about it", just applied to
  // different data (visibility vs. delivery-critical MAC identity).
  static constexpr uint32_t kPeerBindingStaleAfterMs = 5000;

  // Policy v1 - deterministic, not a guess:
  //  1. New GRUT address + MAC            -> binding created.
  //  2. Existing address + SAME MAC       -> lastSeen refreshed.
  //  3. Existing address + DIFFERENT MAC,
  //     current binding still fresh       -> REJECTED. The existing
  //     binding is left untouched; addressMacConflictCount() is
  //     incremented. A fresh binding is never overwritten just
  //     because a second MAC claims the same GRUT address.
  //  4. Existing address + DIFFERENT MAC,
  //     current binding is stale          -> rebind allowed;
  //     rebindCount() is incremented.
  // If the table is full and this is a genuinely new address,
  // droppedNewBindingCount() is incremented and nothing is recorded -
  // existing bindings are never evicted to make room (same philosophy
  // as NeighborTable).
  void recordPeerBinding(uint8_t grutAddr, const uint8_t* mac);

  // True and fills outMac (6 bytes) if a binding currently exists for
  // grutAddr - regardless of freshness (freshness is a recordPeerBinding()
  // concern, not a lookup concern; a caller wanting a fresh MAC only
  // should check age separately if that's ever needed).
  bool lookupPeerMac(uint8_t grutAddr, uint8_t* outMac) const;

  size_t peerBindingCount() const;
  uint32_t droppedNewBindingCount() const;
  uint32_t addressMacConflictCount() const;
  uint32_t rebindCount() const;

  // Sends to whatever MAC is currently bound to nextHopAddr. Returns
  // false immediately - no queuing, no retry, no fallback guess - if
  // no binding exists yet, or if a send is already in flight, or the
  // driver is not running. This is deliberately as simple as
  // sendBroadcastIfIdle(): a raw capability for a future Routing layer
  // to call, not itself a scheduling or reliability policy. No route
  // selection happens here or anywhere yet.
  bool sendToPeer(uint8_t nextHopAddr, const uint8_t* frameBytes,
                  size_t frameLength);

 private:
  struct PeerBinding {
    uint8_t grutAddr = 0;
    bool known = false;
    uint8_t mac[6] = {};
    uint32_t lastSeenMs = 0;
  };

  int findBindingIndex(uint8_t grutAddr) const;

  PeerBinding peerBindings_[kMaxPeerBindings];
  size_t peerBindingCount_ = 0;
  uint32_t droppedNewBindings_ = 0;
  uint32_t addressMacConflicts_ = 0;
  uint32_t rebinds_ = 0;

 private:
  static void onSend(uint8_t* macAddr, uint8_t status);
  static void onReceive(uint8_t* macAddr, uint8_t* data, uint8_t length);

  void trySendNext();
  void recordSendInFlightCleared(uint32_t nowMs);

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

  uint32_t sendInFlightStartMs_ = 0;
  uint32_t sendInFlightMaxAgeMs_ = 0;
  uint32_t sendInFlightOverThresholdCount_ = 0;
};

}  // namespace transport
}  // namespace grut
