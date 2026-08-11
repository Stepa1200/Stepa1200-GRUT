#include "LinkManager.h"

#include <cstddef>

namespace grut {
namespace link {

LinkManager::LinkManager() {
  reset();
}

void LinkManager::reset() {
  state_ = LinkState::kUnknown;
  hasHeartbeat_ = false;
  lastHeartbeatMs_ = 0;
  recoveryHeartbeats_ = 0;

  hasSequence_ = false;
  expectedSequence_ = 0;
  receivedFrames_ = 0;
  sequenceGaps_ = 0;

  sendFailures_ = 0;
  queueDrops_ = 0;

  for (auto& bucket : shortBuckets_) {
    bucket = LossBucket{};
  }
}

void LinkManager::onFrameReceived(uint16_t sequence, uint32_t nowMs) {
  uint32_t gaps = 0;

  if (hasSequence_) {
    const uint16_t forward = static_cast<uint16_t>(sequence - expectedSequence_);

    // A forward distance smaller than half the uint16_t space is treated as a
    // real gap. A larger distance is most plausibly a late/duplicate frame;
    // do not turn that into a catastrophic ~65k false loss count.
    if (forward != 0u && forward < 0x8000u) {
      gaps = forward;
      expectedSequence_ = static_cast<uint16_t>(sequence + 1u);
    } else if (forward == 0u) {
      expectedSequence_ = static_cast<uint16_t>(sequence + 1u);
    }
  } else {
    hasSequence_ = true;
    expectedSequence_ = static_cast<uint16_t>(sequence + 1u);
  }

  ++receivedFrames_;
  sequenceGaps_ += gaps;
  accountShortWindow(/*received=*/1u, gaps, nowMs);
}

void LinkManager::onHeartbeat(uint32_t nowMs) {
  hasHeartbeat_ = true;
  lastHeartbeatMs_ = nowMs;

  switch (state_) {
    case LinkState::kUnknown:
      state_ = LinkState::kUp;
      recoveryHeartbeats_ = 0;
      break;

    case LinkState::kDown:
      state_ = LinkState::kRecovering;
      recoveryHeartbeats_ = 1;
      break;

    case LinkState::kRecovering:
      if (recoveryHeartbeats_ < kRecoveryHeartbeatsRequired) {
        ++recoveryHeartbeats_;
      }
      if (recoveryHeartbeats_ >= kRecoveryHeartbeatsRequired) {
        recoveryHeartbeats_ = 0;
        state_ = LinkState::kUp;
        evaluateNonRecoveryState(nowMs);
      }
      break;

    case LinkState::kUp:
    case LinkState::kDegraded:
      evaluateNonRecoveryState(nowMs);
      break;
  }
}

void LinkManager::onSendFailure(uint32_t count) {
  sendFailures_ += count;
}

void LinkManager::onQueueDrop(uint32_t count) {
  queueDrops_ += count;
}

void LinkManager::poll(uint32_t nowMs) {
  if (!hasHeartbeat_) {
    state_ = LinkState::kUnknown;
    recoveryHeartbeats_ = 0;
    return;
  }

  if (heartbeatAgeMs(nowMs) >= kDownTimeoutMs) {
    state_ = LinkState::kDown;
    recoveryHeartbeats_ = 0;
    return;
  }

  if (state_ == LinkState::kRecovering) {
    return;  // only consecutive heartbeat events may promote recovery to UP
  }

  evaluateNonRecoveryState(nowMs);
}

LinkState LinkManager::state() const {
  return state_;
}

uint8_t LinkManager::recoveryHeartbeatCount() const {
  return recoveryHeartbeats_;
}

uint32_t LinkManager::receivedFrameCount() const {
  return receivedFrames_;
}

uint32_t LinkManager::sequenceGapCount() const {
  return sequenceGaps_;
}

uint32_t LinkManager::sendFailureCount() const {
  return sendFailures_;
}

uint32_t LinkManager::queueDropCount() const {
  return queueDrops_;
}

uint32_t LinkManager::heartbeatAgeMs(uint32_t nowMs) const {
  if (!hasHeartbeat_) {
    return kNoHeartbeatAgeMs;
  }
  return static_cast<uint32_t>(nowMs - lastHeartbeatMs_);
}

uint16_t LinkManager::shortLossPermille(uint32_t nowMs) const {
  uint32_t received = 0;
  uint32_t gaps = 0;
  shortWindowTotals(nowMs, &received, &gaps);
  return lossPermille(received, gaps);
}

uint16_t LinkManager::longLossPermille() const {
  return lossPermille(receivedFrames_, sequenceGaps_);
}

LinkStats LinkManager::snapshot(uint32_t nowMs) const {
  LinkStats out;
  out.state = state_;
  out.heartbeatAgeMs = heartbeatAgeMs(nowMs);
  out.receivedFrames = receivedFrames_;
  out.sequenceGaps = sequenceGaps_;
  out.shortLossPermille = shortLossPermille(nowMs);
  out.longLossPermille = longLossPermille();
  out.sendFailures = sendFailures_;
  out.queueDrops = queueDrops_;
  out.recoveryHeartbeats = recoveryHeartbeats_;
  return out;
}

void LinkManager::accountShortWindow(uint32_t received, uint32_t gaps,
                                     uint32_t nowMs) {
  const uint32_t bucketStart = nowMs - (nowMs % kBucketMs);
  const uint8_t index = static_cast<uint8_t>((nowMs / kBucketMs) %
                                              kShortBucketCount);
  LossBucket& bucket = shortBuckets_[index];
  if (!bucket.valid || bucket.startMs != bucketStart) {
    bucket.startMs = bucketStart;
    bucket.received = 0;
    bucket.gaps = 0;
    bucket.valid = true;
  }
  bucket.received += received;
  bucket.gaps += gaps;
}

void LinkManager::shortWindowTotals(uint32_t nowMs, uint32_t* received,
                                    uint32_t* gaps) const {
  uint32_t receivedTotal = 0;
  uint32_t gapTotal = 0;

  for (const auto& bucket : shortBuckets_) {
    if (!bucket.valid) {
      continue;
    }
    const uint32_t age = static_cast<uint32_t>(nowMs - bucket.startMs);
    if (age < kShortWindowMs) {
      receivedTotal += bucket.received;
      gapTotal += bucket.gaps;
    }
  }

  *received = receivedTotal;
  *gaps = gapTotal;
}

void LinkManager::evaluateNonRecoveryState(uint32_t nowMs) {
  if (!hasHeartbeat_) {
    state_ = LinkState::kUnknown;
    return;
  }

  const uint32_t age = heartbeatAgeMs(nowMs);
  if (age >= kDownTimeoutMs) {
    state_ = LinkState::kDown;
    recoveryHeartbeats_ = 0;
    return;
  }

  // ADR 0007 defines UP below 2500 ms and DOWN at 3000 ms but otherwise
  // leaves the 2500..2999 ms low-loss band uncovered. Treat that near-timeout
  // band as DEGRADED so the state machine remains total and deterministic.
  if (age >= kUpFreshnessMs ||
      shortLossPermille(nowMs) > kDegradedLossPermille) {
    state_ = LinkState::kDegraded;
  } else {
    state_ = LinkState::kUp;
  }
}

uint16_t LinkManager::lossPermille(uint32_t received, uint32_t gaps) {
  const uint64_t total = static_cast<uint64_t>(received) + gaps;
  if (total == 0u) {
    return 0;
  }
  return static_cast<uint16_t>((static_cast<uint64_t>(gaps) * 1000u) / total);
}

}  // namespace link
}  // namespace grut
