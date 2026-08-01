# Universal firmware

## Status

Accepted

## Context

GRUT nodes are deployed in different roles - on the aircraft, on the
ground, or as a relay - but they run on the same ESP8285 hardware.
Maintaining separate firmware codebases per role would multiply the
maintenance burden and risk the codebases drifting apart over time,
especially since BIOS and Transport logic (boot, diagnostics, console,
UART ownership) is identical regardless of role. A single firmware
image that adapts to its role is preferable, as long as role selection
is handled safely and explicitly.

## Decision

- The same firmware binary is intended for all GRUT ESP8285 nodes.
- Supported roles: `AUTO`, `AIR`, `GROUND`, `RELAY`.
- Role selection must be explicit or safely auto-detected - a node must
  never end up in an undefined or ambiguous role.
- The architecture must not require separate Air and Ground codebases.
- Hardware-specific limitations (pin mappings, peripheral availability,
  memory constraints) must be documented rather than silently assumed.
- No role may inject diagnostic text into the MAVLink UART.

## Consequences

**Positive:**

- One firmware image to build, test, and flash across the whole fleet,
  reducing duplicated logic and divergence risk.
- Bug fixes and architectural changes (e.g. BIOS/Transport separation,
  exclusive UART ownership) automatically apply to every role.
- New roles can be added by extending role-selection and role-specific
  behavior, not by forking the codebase.

**Trade-offs / constraints:**

- Role-specific behavior must be cleanly conditional (on a role value
  determined at boot or configuration time) rather than compiled into
  separate binaries, which adds some runtime branching.
- Auto-detection logic must be conservative: an incorrect automatic role
  guess is worse than requiring explicit configuration.
- Every role must respect the same "no diagnostic text into MAVLink
  UART" constraint from ADR 0002/0001, even where that role's normal
  operation would otherwise be console-heavy.
- Hardware differences between deployments (if any) must be documented
  per-role rather than discovered at flash time.
