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

  sendInFlight_ = false;
  running_ = true;
  return true;
}

void EspNowDriver::stop() {
  if (!running_) {
    return;
  }

  esp_now_del_peer(peerMac_);
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
  esp_now_send(peerMac_, buffer, static_cast<int>(length));
}

void EspNowDriver::onSend(uint8_t* /*macAddr*/, uint8_t /*status*/) {
  // ADR 0005: callback must stay minimal - no UART, no parsing, no
  // logging, no retransmission. Just clear the in-flight flag so
  // poll() can send the next queued frame, if any.
  if (g_activeDriver != nullptr) {
    g_activeDriver->sendInFlight_ = false;
  }
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
