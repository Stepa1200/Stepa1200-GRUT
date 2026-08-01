#include "transport/StubTransport.h"

namespace grut {
namespace transport {

bool StubTransport::start() {
  return false;  // intentionally never starts
}

void StubTransport::stop() {
  // Intentionally does nothing.
}

bool StubTransport::isRunning() const {
  return false;
}

void StubTransport::poll() {
  // Intentionally does nothing.
}

bool StubTransport::send(const uint8_t* /*data*/, size_t /*length*/) {
  return false;
}

const char* StubTransport::name() const {
  return "none (stub)";
}

}  // namespace transport
}  // namespace grut
