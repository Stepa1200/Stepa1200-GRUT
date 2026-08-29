# ENGINEERING_PLAYBOOK.md

> Purpose: shared operating rules for the user, Claude, ChatGPT, and any
> future coding agent working on ESP GRUT. This file covers workflow,
> gates, and evidence discipline only. Architecture facts (layer
> boundaries, UART ownership, protocol rules, callback policy, resource
> constraints) live in the ADRs listed in Section 4 — this file does
> not repeat them, so they cannot silently drift out of sync.

# 1. Roles

## User / hardware operator / final authority

Owns:

- physical wiring
- flashing unless explicitly delegated
- power application
- propeller/flight safety
- real-hardware measurements
- approval of destructive/repository-changing actions when required
- final product priorities
- closing GitHub Issues (see Section 6 — no AI closes an Issue, ever)
- merging any branch into `main` (see Section 7)

The workflow should minimize the user's mechanical copy/paste work, but
not remove the user from hardware, safety, or repository-finality
decisions.

## Claude / implementation agent

Primary responsibilities:

- inspect current repository state before editing
- implement scoped tasks from the active-task Issue
- keep layer boundaries intact (per the ADRs in Section 4)
- run native tests
- build all affected ESP targets
- report exact changed files/results as Issue comments
- prepare exact hardware-test instructions when a hardware gate is
  reached
- never assume ChatGPT's previous chat context — the repository and
  the active-task Issue are the shared context

## ChatGPT / architecture and review agent

Primary responsibilities:

- review current repository state and project documentation directly
- define the next smallest architecture-safe milestone
- identify compatibility and hardware-safety risks
- review Claude's implementation reports and commits
- propose the next Issue when the user approves

ChatGPT must not assume Claude's private conversation context. The
repository and Issues are the shared context.

# 2. GitHub is the bridge

The shared asynchronous interface between models is the repository and
its Issues — not manual chat transcript copying.

Every AI session should begin by reading, in this order:

1. `GRUT_STATE.md` (pointer only — see its own header)
2. `PROJECT_ROADMAP.md`
3. `ENGINEERING_PLAYBOOK.md` (this file)
4. the Issue labeled `active-task`
5. relevant ADRs (see Section 4)
6. `docs/PROTOCOL.md` if protocol work is involved
7. current code/configuration
8. recent commits relevant to the active task

If repository state conflicts with chat memory, repository + real
hardware evidence win.

# 3. Source-of-truth hierarchy

Prefer evidence in this order:

1. reproducible results from the user's real hardware
2. current GRUT repository code/configuration
3. GRUT ADRs/docs
4. official manufacturer/framework documentation
5. inference

Never silently convert inference into a hardware fact.

Use explicit labels:

- CONFIRMED
- INFERRED
- UNKNOWN
- NEEDS MEASUREMENT

# 4. Architecture reference (do not duplicate here)

Layer boundaries, UART ownership, GRUT protocol rules, ESP-NOW callback
policy, and resource constraints for ESP8266/ESP8285-class hardware are
authoritative in:

- `docs/ADR/0001-bios-transport-separation.md`
- `docs/ADR/0002-exclusive-uart-ownership.md`
- `docs/ADR/0005-transport-frame-pipeline-and-bios-freeze.md`
- `docs/ADR/0006-esp-now-driver-design.md`
- `docs/ADR/0007-link-manager-v1.md`
- `docs/ADR/0008-hello-discovery-and-data-source-safety-gate.md`
- `docs/ADR/0009-endpoint-conflict-policy-v1.md`
- `docs/ADR/0010-route-policy-v1.md`

If an implementation task appears to require restating one of these
facts, link the ADR instead of copying its content. If an ADR is
missing for a decision that clearly changed architecture, write the
ADR before or alongside the implementation, not after the fact from
memory — see the note below on how the previous gap was closed.

## 4a. Resolved gap (informational)

As of this document's original adoption, three implemented decisions
lacked a dedicated ADR: HELLO discovery + DATA-source safety gate
(Stage 4.2), endpoint conflict policy v1 (Stage 5.0), and route policy
v1 (Stage 5.1). These are now documented in ADR 0008, 0009, and 0010
respectively (Issue #2). This subsection is kept only as a record that
the process caught and closed its own documentation debt; remove it
entirely once it no longer serves that purpose.

# 5. Debugging method

Change one variable at a time.

Use:

```text
OBSERVATION
-> HYPOTHESIS
-> ISOLATED TEST
-> RESULT
-> CONCLUSION
```

Prefer tests that split the path into halves.

Example:

```text
FC -> AIR -> radio -> GROUND -> PC
```

Test independently:

- FC/UART
- AIR UART input
- ESP-NOW
- GROUND UART output
- PC/Mission Planner

Readable STATUSTEXT proves bytes crossed the bridge; it does not by
itself prove lossless MAVLink transport.

# 6. Code-change workflow

Before changing code, the implementing AI must state, as an Issue
comment:

1. exact failure or requirement
2. responsible layer
3. expected files to change
4. compatibility impact
5. tests that will prove the change

Avoid unrelated refactoring.

After changing code:

1. run relevant native tests
2. build affected ESP targets
3. report exact results as an Issue comment
4. report changed files
5. distinguish BUILD-TESTED from HARDWARE-VERIFIED
6. do not flash automatically
7. do not commit automatically unless the task explicitly authorizes it
8. do not push automatically unless the task explicitly authorizes it
   (see Section 7 for the standing exception under review)

# 7. Task branches and push/merge permission

Proposed transport for implementation work:

```text
Issue -> task branch -> CI -> review -> user approval -> main
```

**Current state: not yet enabled.** All commits/pushes still require
explicit per-action user authorization, exactly as before this
document. This section documents the target model for future adoption,
not a current standing permission.

Target model once adopted:

- Claude may be granted **standing authorization to push to task
  branches only** (branches created for one Issue, never `main`).
- Claude is **never** authorized to push to or merge into `main`,
  standing or otherwise.
- Merging a task branch into `main` remains a user-approved action,
  every time, regardless of CI status.
- CI passing is a precondition for requesting merge approval, not a
  substitute for it.

# 8. Testing policy

Every pure algorithmic component should be host-testable when
practical.

Examples:

- framing/protocol codec
- queues
- LinkManager state machine
- NeighborTable
- endpoint binding policy
- RouteTable

A successful compilation is never proof that hardware behavior works.

Use:

- BUILD-TESTED
- HARDWARE-VERIFIED

as separate statements, and CI results (Section 10) as the mechanical
check for the former.

# 9. Hardware procedure policy

For hands-on work, provide one practical step at a time and wait for
the result.

For any measurement, state:

- where to measure
- expected possibilities
- what each result means

Never recommend uncertain power wiring based on wire color, connector
position, or a visually similar board.

Before recommending wiring verify:

- voltage level
- pin function
- UART mapping
- common GND
- power capability
- TX/RX direction
- exact board/revision when relevant

# 10. Continuous integration

A GitHub Actions workflow runs on every push to a task branch and
every pull request targeting `main`:

- `pio test -e native`
- `pio run -e esp8285-air1`
- `pio run -e esp8285-air2`
- `pio run -e esp8285-ground`
- `pio run -e esp8285-ground-linkdiag`

CI proves BUILD-TESTED mechanically. It never proves HARDWARE-VERIFIED
— that label still requires a real hardware result reported by the
user (see `GRUT_STATE.md` and the active-task Issue).

See the proposed workflow file for exact configuration.

# 11. GitHub Issue and label conventions

## 11.1 `active-task` label

There should normally be exactly **one** open Issue labeled
`active-task` at any time. This is the single task an implementing AI
should be working from. If more than one Issue carries this label,
that is itself a problem to flag and resolve before continuing
implementation work.

## 11.2 Issue lifecycle

- Any AI (Claude, ChatGPT, or another agent) may comment on an Issue:
  progress reports, test/build results, questions, proposed next
  steps.
- An AI may add or remove labels that reflect implementation status
  (e.g. `status:build-tested`) if the repository's label set defines
  them for this purpose.
- **An AI must never close an Issue.** Closing an Issue — including
  one that is HARDWARE-VERIFIED and fully merged — is a user action
  only. This mirrors the existing commit/push/flash gates: task
  completion is a repository-finality decision, not an implementation
  one.
- The user closes the Issue once satisfied, and (per the target model
  in Section 7, once adopted) approves the merge to `main` if one is
  pending.

## 11.3 Issue template

See the proposed Issue template file for the standard shape of an
engineering-task Issue. It mirrors the report structure already used
in this workflow (requirement, responsible layer, files, compatibility
impact, tests — before code; results, changed files, BUILD-TESTED vs
HARDWARE-VERIFIED — after).

# 12. Recommended minimal user interaction

Once these documents are adopted, a typical user instruction to Claude
can be as short as:

```text
Pull latest main. Read GRUT_STATE.md, PROJECT_ROADMAP.md,
ENGINEERING_PLAYBOOK.md, the active-task Issue, and relevant ADRs.
Execute the active task exactly as written.
Stop at the first approval/hardware gate.
```

A typical user instruction to ChatGPT can be:

```text
Read the latest GRUT repository state and the active-task Issue.
Review the completed work. Propose the next Issue if appropriate.
```

This keeps GitHub as the shared context instead of the user acting as
a message bus.

# 13. Escalation rule

Stop and ask for an explicit decision if any task would:

- change GRUT wire format
- break compatibility with existing ESP8285 hardware
- reopen BIOS scope
- parse MAVLink in Transport
- change UART ownership
- introduce routing/relay/mesh earlier than the roadmap
- introduce a new power/wiring assumption
- require destructive hardware or repository action not already
  authorized
- close an Issue or merge into `main` (always the user's action —
  see Sections 7 and 11.2)
