#include "transport/EspNowDriver.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <cstring>

extern "C" {
#include <espnow.h>
#include <user_interface.h>
}

namespace grut {
namespace transport {

namespace {

// ESP-NOW callbacks (espnow.h's esp_now_recv_cb_t/esp_now_send_cb_t)
// are plain C function pointers with no user-data parameter, so the
// active driver instance is reached through this single static
// pointer. Only one EspNowDriver may be running at a time (see class
// comment in EspNowDriver.h).
EspNowDriver* g_activeDriver = nullptr;

}  // namespace

EspNowDriver::EspNowDriver(uint8_t channel, const uint8_t peerMac[6])
    : channel_(channel) {
  std::memcpy(peerMac_, peerMac, sizeof(peerMac_));
}

bool EspNowDriver::start() {
  if (running_) {
    return true;
  }

  g_activeDriver = this;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(channel_);

  if (esp_now_init() != 0) {
    g_activeDriver = nullptr;
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(&EspNowDriver::onSend);
  esp_now_register_recv_cb(&EspNowDriver::onReceive);

  if (esp_now_add_peer(peerMac_, ESP_NOW_ROLE_COMBO, channel_, nullptr, 0) !=
      0) {
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    g_activeDriver = nullptr;
    return false;
  }

  // Stage 4.2: register the broadcast MAC too, per Espressif's own
  // guidance ("a device with broadcast MAC address must be added
  // before sending broadcast data"). This does not change anything
  // about the fixed-peer relationship above - it only enables
  // sendBroadcastIfIdle() for discovery. Deliberately not fatal if
  // this fails: broadcast discovery is best-effort by design (see
  // sendBroadcastIfIdle()), so a node that couldn't register the
  // broadcast peer still starts up and works normally as before,
  // simply without outbound HELLO capability this run.
  uint8_t broadcastMac[6];
  std::memcpy(broadcastMac, kBroadcastMac, sizeof(broadcastMac));
  esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, channel_, nullptr, 0);

  sendInFlight_ = false;
  running_ = true;
  return true;
}

void EspNowDriver::stop() {
  if (!running_) {
    return;
  }

  esp_now_del_peer(peerMac_);
  {
    uint8_t broadcastMac[6];
    std::memcpy(broadcastMac, kBroadcastMac, sizeof(broadcastMac));
    esp_now_del_peer(broadcastMac);
  }
  esp_now_unregister_send_cb();
  esp_now_unregister_recv_cb();
  esp_now_deinit();

  g_activeDriver = nullptr;
  running_ = false;
  sendInFlight_ = false;
}

bool EspNowDriver::isRunning() const {
  return running_;
}

uint8_t EspNowDriver::currentWifiChannel() const {
  return wifi_get_channel();
}

void EspNowDriver::poll() {
  if (!running_) {
    return;
  }
  trySendNext();
}

bool EspNowDriver::send(const uint8_t* frameBytes, size_t frameLength) {
  if (!running_) {
    return false;
  }
  const bool queued = sendQueue_.push(frameBytes, frameLength);
  trySendNext();
  return queued;
}

bool EspNowDriver::sendIfIdle(const uint8_t* frameBytes, size_t frameLength) {
  if (!running_ || sendInFlight_ || !sendQueue_.empty()) {
    return false;
  }
  // No normal DATA is queued, so this best-effort frame cannot displace an
  // already-buffered transport frame. If a DATA burst arrives immediately
  // afterwards it still gets the full normal FrameQueue capacity.
  const bool queued = sendQueue_.push(frameBytes, frameLength);
  if (!queued) {
    return false;
  }
  trySendNext();
  return true;
}

bool EspNowDriver::sendBroadcastIfIdle(const uint8_t* frameBytes,
                                       size_t frameLength) {
  // Same idle gate as sendIfIdle(): never contend with a DATA burst or
  // an already in-flight send. Bypasses sendQueue_ entirely - a missed
  // HELLO cycle is harmless (the next periodic attempt tries again),
  // so there is nothing to queue or retry.
  if (!running_ || sendInFlight_ || !sendQueue_.empty()) {
    return false;
  }

  uint8_t buffer[FrameQueue::kMaxFrameBytes];
  if (frameLength > sizeof(buffer)) {
    return false;
  }
  std::memcpy(buffer, frameBytes, frameLength);

  uint8_t broadcastMac[6];
  std::memcpy(broadcastMac, kBroadcastMac, sizeof(broadcastMac));

  sendInFlight_ = true;
  sendInFlightStartMs_ = millis();
  ++sendAttempted_;

  const int result =
      esp_now_send(broadcastMac, buffer, static_cast<int>(frameLength));
  if (result != 0) {
    // Same reasoning as trySendNext(): no callback will fire for a
    // send esp_now_send() itself rejected, so clear immediately.
    sendInFlight_ = false;
    ++sendImmediateErrors_;
    recordSendInFlightCleared(millis());
  }
  return result == 0;
}

bool EspNowDriver::txIdle() const {
  return !sendInFlight_ && sendQueue_.empty();
}

bool EspNowDriver::receive(uint8_t* outBuffer, size_t outCapacity,
                            size_t* outLength) {
  return recvQueue_.pop(outBuffer, outCapacity, outLength);
}

uint32_t EspNowDriver::droppedSendCount() const {
  return sendQueue_.droppedCount();
}

uint32_t EspNowDriver::droppedReceiveCount() const {
  return recvQueue_.droppedCount();
}

uint32_t EspNowDriver::sendAttemptedCount() const {
  return sendAttempted_;
}

uint32_t EspNowDriver::sendImmediateErrorCount() const {
  return sendImmediateErrors_;
}

uint32_t EspNowDriver::sendCallbackSuccessCount() const {
  return sendCallbackSuccesses_;
}

uint32_t EspNowDriver::sendCallbackFailureCount() const {
  return sendCallbackFailures_;
}

void EspNowDriver::trySendNext() {
  if (sendInFlight_ || sendQueue_.empty()) {
    return;
  }

  uint8_t buffer[FrameQueue::kMaxFrameBytes];
  size_t length = 0;
  if (!sendQueue_.pop(buffer, sizeof(buffer), &length)) {
    return;
  }

  sendInFlight_ = true;
  sendInFlightStartMs_ = millis();
  ++sendAttempted_;

  const int result = esp_now_send(peerMac_, buffer, static_cast<int>(length));
  if (result != 0) {
    // esp_now_send() failed immediately - no callback will ever fire
    // for this attempt. Clearing sendInFlight_ here is essential:
    // without it, the driver would wait forever for a callback that
    // was never coming, and every subsequent frame would queue up
    // and eventually be dropped once the (never-draining) queue fills.
    sendInFlight_ = false;
    ++sendImmediateErrors_;
    recordSendInFlightCleared(millis());
  }
}

void EspNowDriver::onSend(uint8_t* /*macAddr*/, uint8_t status) {
  // ADR 0005: callback must stay minimal - no UART, no parsing, no
  // logging, no retransmission. Just clear the in-flight flag so
  // poll() can send the next queued frame, if any, and tally the
  // result for diagnostics. recordSendInFlightCleared() is O(1)
  // arithmetic on already-in-memory fields - no allocation, no I/O.
  if (g_activeDriver != nullptr) {
    g_activeDriver->sendInFlight_ = false;
    if (status == 0) {
      ++g_activeDriver->sendCallbackSuccesses_;
    } else {
      ++g_activeDriver->sendCallbackFailures_;
    }
    g_activeDriver->recordSendInFlightCleared(millis());
  }
}

void EspNowDriver::recordSendInFlightCleared(uint32_t nowMs) {
  const uint32_t age = nowMs - sendInFlightStartMs_;
  if (age > sendInFlightMaxAgeMs_) {
    sendInFlightMaxAgeMs_ = age;
  }
  if (age > kSendInFlightObservationThresholdMs) {
    ++sendInFlightOverThresholdCount_;
  }
}

uint32_t EspNowDriver::sendInFlightCurrentAgeMs(uint32_t nowMs) const {
  if (!sendInFlight_) {
    return 0;
  }
  return nowMs - sendInFlightStartMs_;
}

uint32_t EspNowDriver::sendInFlightMaxAgeMs() const {
  return sendInFlightMaxAgeMs_;
}

uint32_t EspNowDriver::sendInFlightOverThresholdCount() const {
  return sendInFlightOverThresholdCount_;
}

void EspNowDriver::onReceive(uint8_t* /*macAddr*/, uint8_t* data,
                              uint8_t length) {
  // ADR 0005: minimal bounds validation (FrameQueue::push() itself
  // rejects anything over kMaxFrameBytes) + enqueue only. No parsing,
  // no UART access here.
  if (g_activeDriver != nullptr) {
    g_activeDriver->recvQueue_.push(data, length);
  }
}

}  // namespace transport
}  // namespace grut
