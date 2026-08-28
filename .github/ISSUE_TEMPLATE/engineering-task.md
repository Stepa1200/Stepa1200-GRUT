---
name: Engineering task
about: A scoped implementation task for Claude (or another agent) to execute
title: "Stage X.Y — <short task name>"
labels: active-task
assignees: ""
---

<!--
Before opening: check that no other open Issue already carries the
active-task label. There should normally be exactly one.
-->

## Goal

<!-- One or two sentences. What should be true when this is done. -->

## Current hardware-verified foundation

<!-- What already exists and is verified, that this task builds on.
     Link to PROJECT_ROADMAP.md stage(s) if relevant. -->

## Architectural responsibility

<!-- Which layer owns this (Transport / LinkManager / NeighborTable /
     Routing / Desktop / etc). Reference the relevant ADR(s) if one
     already exists for this layer. -->

## Requirements

-
-

## Strictly not yet

<!-- Explicitly excluded scope, to prevent creep into later stages. -->

-
-

## Before coding — required report (as a comment on this Issue)

The implementing AI must post a comment covering:

1. Exact requirement (restated in its own words, to confirm shared
   understanding).
2. Responsible layer.
3. Exact files inspected.
4. Exact files proposed to change/create.
5. Whether wire-format compatibility is affected.
6. Proposed smallest isolated change.

Do not start coding until this report is posted, unless the task
explicitly says otherwise.

## Tests required

<!-- List the specific scenarios that must be covered by host tests
     and/or behavioral checks. -->

-
-

## After coding — required report (as a comment on this Issue)

1. Native test results (exact counts, per suite if there are several).
2. Build results for every affected ESP target.
3. Exact changed/created files.
4. Compatibility impact statement.
5. Explicit **BUILD-TESTED** vs **HARDWARE-VERIFIED** classification —
   never claim the latter from tests/builds alone.

## Hardware gate

<!-- State explicitly: "None for this task" OR the exact single
     physical step the user needs to perform, with what to look for. -->

## Stop conditions

- Do not flash automatically.
- Do not commit automatically unless this Issue explicitly authorizes
  it.
- Do not push automatically unless this Issue explicitly authorizes
  it (see `ENGINEERING_PLAYBOOK.md` Section 7 for the task-branch
  permission model).
- Do not close this Issue. Closure is a user action only.
