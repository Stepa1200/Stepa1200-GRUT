# Plugin architecture

## Status

Accepted

## Context

GRUT needs to identify and interoperate with flight controllers and
sensors from many vendors and stacks (ArduPilot, PX4, Betaflight, INAV,
STM32-based boards, ESP-based boards, IMUs, SPI devices). Hard-coding
vendor-specific detection and handling logic directly into the core
would make the core grow without bound as support is added, and would
force every build to carry every vendor's logic whether or not it is
needed on a given node.

## Decision

- Flight-controller support must be implemented as plugins where
  practical.
- Planned plugins include: ArduPilot, PX4, Betaflight, INAV, STM32, ESP,
  IMU, and SPI.
- Core modules must not hard-code vendor-specific detection logic.
- Plugins must expose versioned interfaces.
- The core must remain usable when an optional plugin is absent.

## Consequences

**Positive:**

- New vendor/stack support can be added as a new plugin without
  modifying core code.
- The core stays small and stable; its behavior does not change as
  plugins are added, removed, or updated.
- A node can be built or configured with only the plugins it actually
  needs.

**Trade-offs / constraints:**

- Plugin interfaces must be designed and versioned carefully up front -
  a breaking interface change affects every plugin at once.
- Vendor-specific quirks that don't fit the plugin interface cleanly
  will need either an interface extension (with a version bump) or a
  documented exception, rather than a quick core-side special case.
- The core needs an explicit mechanism for "plugin absent" behavior
  (graceful degradation) for every capability that could otherwise
  assume a plugin is present.
