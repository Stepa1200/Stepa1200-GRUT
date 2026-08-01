#include "RuntimeManager.h"

namespace grut {
namespace bios {

RuntimeManager::RuntimeManager(IConsole& console, transport::ITransport& transport)
    : console_(console), transport_(transport) {}

bool RuntimeManager::enableTransport() {
  console_.stop();

  if (!transport_.start()) {
    console_.start();
    return false;
  }

  return true;
}

void RuntimeManager::disableTransport() {
  transport_.stop();
  console_.start();
}

bool RuntimeManager::consoleRunning() const {
  return console_.isRunning();
}

bool RuntimeManager::transportRunning() const {
  return transport_.isRunning();
}

}  // namespace bios
}  // namespace grut
