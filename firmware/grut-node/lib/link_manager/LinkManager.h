#pragma once

#include <cstdint>

namespace grut {
namespace link {

enum class LinkState : uint8_t {
  kUnknown = 0,
  kUp,
  kDegraded,
  kDown,
  kRecovering,
};

struct LinkStats {
  LinkState state = LinkState::kUnknown;
  uint32_t heartbeatAgeMs = 0xFFFFFFFFu;
  uint32_t receivedFrames = 0;
  uint32_t sequenceGaps = 0;
  uint16_t shortLossPermille = 0;
  uint16_t longLossPermille = 0;
  uint32_t sendFailures = 0;
  uint32_t queueDrops = 0;
  uint8_t recoveryHeartbeats = 0;
};

// Pure, host-testable link-health state machine (ADR 0007).
//
// LinkManager does not own UART, ESP-NOW, timers, logging, recovery actions,
// or any vehicle failsafe behavior. Callers feed it facts (valid GRUT frame
// sequences, valid heartbeat reception, send failures, queue drops, and
// current monotonic millis). It only derives health state and counters.
class LinkManager {
 public:
  static constexpr uint32_t kHeartbeatIntervalMs = 1000;
  static constexpr uint32_t kUpFreshnessMs = 2500;
  static constexpr uint32_t kDownTimeoutMs = 3000;
  static constexpr uint32_t kShortWindowMs = 10000;
  static constexpr uint16_t kDegradedLossPermille = 50;  // 5.0%
  static constexpr uint8_t kRecoveryHeartbeatsRequired = 3;
  static constexpr uint32_t kNoHeartbeatAgeMs = 0xFFFFFFFFu;

  LinkManager();

  // Return to boot state and clear all counters/windows.
  void reset();

  // Account one valid decoded GRUT frame. Must be called for every frame type
  // before packet-type dispatch so the node-wide sequence has one authority.
  void onFrameReceived(uint16_t sequence, uint32_t nowMs);

  // Account one valid kHeartbeat frame. Sequence accounting remains separate:
  // the caller should call onFrameReceived() for that same frame first.
  void onHeartbeat(uint32_t nowMs);

  void onSendFailure(uint32_t count = 1u);
  void onQueueDrop(uint32_t count = 1u);

  // Re-evaluate time/loss-driven state transitions at caller-supplied time.
  void poll(uint32_t nowMs);

  LinkState state() const;
  uint8_t recoveryHeartbeatCount() const;
  uint32_t receivedFrameCount() const;
  uint32_t sequenceGapCount() const;
  uint32_t sendFailureCount() const;
  uint32_t queueDropCount() const;
  uint32_t heartbeatAgeMs(uint32_t nowMs) const;
  uint16_t shortLossPermille(uint32_t nowMs) const;
  uint16_t longLossPermille() const;
  LinkStats snapshot(uint32_t nowMs) const;

 private:
  struct LossBucket {
    uint32_t startMs = 0;
    uint32_t received = 0;
    uint32_t gaps = 0;
    bool valid = false;
  };

  static constexpr uint8_t kShortBucketCount = 10;
  static constexpr uint32_t kBucketMs = 1000;

  void accountShortWindow(uint32_t received, uint32_t gaps, uint32_t nowMs);
  void shortWindowTotals(uint32_t nowMs, uint32_t* received,
                         uint32_t* gaps) const;
  void evaluateNonRecoveryState(uint32_t nowMs);
  static uint16_t lossPermille(uint32_t received, uint32_t gaps);

  LinkState state_ = LinkState::kUnknown;
  bool hasHeartbeat_ = false;
  uint32_t lastHeartbeatMs_ = 0;
  uint8_t recoveryHeartbeats_ = 0;

  bool hasSequence_ = false;
  uint16_t expectedSequence_ = 0;
  uint32_t receivedFrames_ = 0;
  uint32_t sequenceGaps_ = 0;

  uint32_t sendFailures_ = 0;
  uint32_t queueDrops_ = 0;

  LossBucket shortBuckets_[kShortBucketCount];
};

}  // namespace link
}  // namespace grut
