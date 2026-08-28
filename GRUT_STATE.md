# GRUT_STATE.md

> Purpose: a stable pointer to where the current state actually lives.
> This file is not a task log and should change rarely — normally
> never, once adopted.

Current roadmap/status: see `PROJECT_ROADMAP.md`

Active task: the single open GitHub Issue labeled `active-task`

Workflow/gates: see `ENGINEERING_PLAYBOOK.md`

Repository state: always inspect current `main` directly

## Do not store here

- current commit SHA
- current stage
- hardware-verified milestone
- task details

All of the above live in `PROJECT_ROADMAP.md`, the `active-task`
Issue, or `main` itself — never duplicated into this file.

