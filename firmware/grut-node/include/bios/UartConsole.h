#pragma once

#include "bios/IConsole.h"

namespace grut {
namespace bios {

// Console over the physical debug UART (Serial).
//
// Fallback/debug interface only. Valid for BIOS v0.2 where Transport is
// still a disabled stub. Once a real UART Transport exists, this console
// must not run at the same time Transport owns the physical UART - the
// primary interface then becomes GRUT Desktop, with TcpConsole as the
// no-GUI fallback (see CLAUDE.md).
class UartConsole : public IConsole {
 public:
  void begin() override;
  int available() override;
  int read() override;
  size_t write(const uint8_t* data, size_t length) override;
  void flush() override;
};

}  // namespace bios
}  // namespace grut
