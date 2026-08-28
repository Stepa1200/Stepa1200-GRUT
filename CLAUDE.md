# CLAUDE.md

This file is intentionally minimal. It does not describe the current
project stage, roadmap, or architecture — those live elsewhere and
this file must not drift out of sync with them again.

Before doing anything else in this repository, read, in this order:

1. `PROJECT_ROADMAP.md` — current stage, exit criteria, status.
2. `ENGINEERING_PLAYBOOK.md` — workflow, evidence rules, approval
   gates, testing policy, Issue/label conventions.
3. The GitHub Issue labeled `active-task` — the specific task to
   execute right now, including its exact requirement, constraints,
   and stop conditions.
4. Any ADR or doc the active-task Issue or the playbook points to for
   this specific task (see `docs/ADR/` and `docs/PROTOCOL.md`).
5. The current code relevant to the task — inspect it directly rather
   than relying on a prior session's description of it.

Then execute the active task exactly as written, following
`ENGINEERING_PLAYBOOK.md`'s workflow: report before coding, implement,
test, report results, and stop at the first approval or hardware gate.

Do not assume any milestone, file content, or repository state from a
previous conversation. Verify directly against current `main`.
