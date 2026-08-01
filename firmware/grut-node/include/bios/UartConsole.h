#pragma once

#include "bios/IConsole.h"

namespace grut {
namespace bios {

// Console over the physical debug UART (Serial).
//
// Exclusively owns Serial only while running() - RuntimeManager
// guarantees this never overlaps with UartTransport owning the same
// peripheral (see lib/runtime_manager/RuntimeManager, CLAUDE.md).
class UartConsole : public IConsole {
 public:
  bool start() override;
  void stop() override;
  bool isRunning() const override;
  void poll() override;
  int available() override;
  int read() override;
  size_t write(const uint8_t* data, size_t length) override;
  void flush() override;

 private:
  bool running_ = false;
};

}  // namespace bios
}  // namespace grut
