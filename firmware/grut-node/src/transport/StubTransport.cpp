#include "transport/StubTransport.h"

namespace grut {
namespace transport {

void StubTransport::begin() {
  // Intentionally does nothing: v0.1 ships with no active transport.
}

void StubTransport::poll() {
  // Intentionally does nothing.
}

bool StubTransport::isEnabled() const {
  return false;
}

bool StubTransport::send(const uint8_t* /*data*/, size_t /*length*/) {
  return false;
}

const char* StubTransport::name() const {
  return "none (stub)";
}

}  // namespace transport
}  // namespace grut
