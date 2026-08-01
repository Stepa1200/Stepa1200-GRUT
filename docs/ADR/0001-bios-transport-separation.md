# BIOS and Transport separation

## Status

Accepted

## Context

GRUT nodes need to run boot, diagnostics, configuration, and an
interactive command console on the same firmware image that also has to
move data over UART, ESP-NOW, UDP, and later mesh routing. If these two
concerns are mixed into one module, every change to a communication
protocol risks breaking boot/diagnostics/console behavior, and vice
versa. A clear boundary is needed so each side can be built, changed,
and tested independently.

## Decision

Split the firmware into two layers with a hard boundary between them:

- **GRUT BIOS** owns boot, diagnostics, configuration, the command
  console, logging, role management, and Transport lifecycle.
- **GRUT Transport** owns UART, ESP-NOW, UDP, routing, relay, mesh, TTL,
  CRC, and queues.

Constraints:

- BIOS must not contain routing logic.
- Transport must not contain BIOS logic.
- Communication between the two layers happens only through interfaces
  and events, never through direct knowledge of the other layer's
  internals.

## Consequences

**Positive:**

- BIOS can be built, flashed, and tested with no real transport wired
  in (a disabled stub satisfies the interface).
- Transport implementations (UART bridge, ESP-NOW, UDP, mesh) can be
  added or swapped without touching BIOS boot/diagnostics/console code.
- Each side has an independent, narrower surface to reason about and
  unit-test.

**Trade-offs / constraints:**

- Every new capability must be classified as either BIOS or Transport
  responsibility before being written; ambiguous features need an
  explicit decision rather than being bolted onto whichever module is
  convenient.
- Cross-layer communication that doesn't fit cleanly into "interface or
  event" requires design work up front instead of a quick direct call.
- Two separate module boundaries mean slightly more indirection
  (interfaces, injected dependencies) than a single monolithic file
  would need.
