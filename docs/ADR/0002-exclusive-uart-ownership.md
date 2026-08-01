# Exclusive UART ownership

## Status

Accepted

## Context

GRUT BIOS and GRUT Transport can both need the same physical UART: BIOS
for its interactive debug console, Transport for carrying real data
(e.g. a MAVLink bridge). A single UART peripheral cannot be read and
written by two independent owners at once without corrupting both
streams. The system needs a rule that guarantees only one owner holds
the physical UART at any given moment, plus a safe way to hand
ownership back and forth without ever leaving the device with no
working interface at all.

## Decision

- Physical UART belongs exclusively to Transport while Transport is
  running.
- BIOS must not write to the physical UART while Transport is active.
- BIOS accesses consoles only through the `IConsole` interface, never
  through a concrete UART type directly.
- `UartConsole` is a temporary `IConsole` implementation, expected to be
  joined or replaced by other implementations (e.g. a TCP console) as
  the system grows.
- `RuntimeManager` performs the ownership handoff in this order:
  - Activating Transport: `console.stop(); transport.start();`
  - Shutting Transport down: `transport.stop(); console.start();`
- If `Transport::start()` fails, the console must be restored
  automatically so BIOS is never left without any interface.

## Consequences

**Positive:**

- No possible byte-level collision between BIOS console output and
  Transport data on the same physical UART, by construction rather than
  by convention.
- The handoff order is centralized in one place (`RuntimeManager`),
  making it possible to unit-test the transition logic (call order,
  rollback on failure) independent of any hardware.
- BIOS code never has to reason about Transport's running state itself
  (no `if (!transport.isRunning()) { ... }` scattered through BIOS
  logic); `IConsole` structurally does nothing while stopped.

**Trade-offs / constraints:**

- While Transport is active, the BIOS console is completely unavailable
  over UART - the device is "mute" for interactive debugging until
  Transport is stopped again or an alternative console (e.g. TCP) is
  available.
- Any new console or transport implementation must respect the same
  start/stop/isRunning contract, or the exclusivity guarantee breaks.
- Failure handling (rollback) adds a code path that must itself stay
  correct and tested, rather than being an afterthought.
