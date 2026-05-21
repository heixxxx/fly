---
name: pre-commit-check
description: Pre-commit gate that analyzes code changes, checks documentation consistency, presents diff summary to user for confirmation, updates docs if needed, then commits. Automatically triggers when user says 'commit', '提交', 'push', or '推送'.
---

# Pre-commit Check

Prevents documentation-drift commits. Every commit must pass a documentation consistency gate.

## Trigger

Automatically activate when user requests: commit, 提交, push, 推送, or any git commit intent.

## Workflow

### Step 1: Gather Changes

```
1. git status — identify modified/new/deleted files
2. git diff — staged + unstaged changes
3. git log --oneline -5 — recent commit style
```

### Step 2: Classify Changes

Categorize every changed file:

| Category | Examples |
|----------|----------|
| **API change** | New/removed/renamed public methods, changed signatures, new structs/classes |
| **Behavior change** | Different runtime behavior, new config, changed defaults |
| **Internal change** | Refactor, performance, private method changes |
| **Test only** | New/modified tests, no production code |
| **Docs only** | Markdown files only |
| **Build only** | BUILD files, .bazelrc, WORKSPACE |

### Step 3: Documentation Consistency Check

For each **API change** or **Behavior change**, check:

**CLAUDE.md**:
- [ ] Module table (§7) — file descriptions accurate?
- [ ] Key API sections — method signatures/names match?
- [ ] Message type count — matches `message_types.h` enum?
- [ ] Config defaults — match `config.cpp`?
- [ ] Architecture flow — matches actual message/data flow?

**docs/**:
- [ ] `docs/<module>/module.md` — class methods, signatures, new/removed members
- [ ] `docs/architecture.md` — message types table, flow descriptions
- [ ] `docs/python-api/module.md` — Python-exposed methods match exports
- [ ] `docs/network/module.md` — message count, new message types

### Step 4: Present Summary to User

Present this format:

```
## Commit Summary

**Files changed**: N files (+X/-Y lines)
**Categories**: API(N) Behavior(N) Internal(N) Test(N) Docs(N)

### Code Changes
- [file]: [what changed]

### Documentation Check
- [x] CLAUDE.md — [up to date / needs update: reason]
- [x] docs/xxx/module.md — [up to date / needs update: reason]
- [x] docs/architecture.md — [up to date / needs update: reason]

### Required Doc Updates (if any)
1. [specific file]: [what to add/change]
2. ...

### Proposed Commit Message
[type]: [message]
```

### Step 5: Wait for User Confirmation

Ask user:
- Approve commit as-is?
- Update docs first? (then update and re-present)
- Modify commit message?

### Step 6: Execute

Only after user confirms:
1. If docs need update → update docs, present diff, get re-confirmation
2. `git add` relevant files
3. `git commit` with approved message
4. If user said push → `git push`

## Rules

- NEVER commit without user confirmation
- NEVER skip the documentation check
- If only tests/docs changed → still present summary but skip doc consistency check
- Match existing commit message style from git log
- If changes span multiple concerns → suggest splitting into multiple commits
- Do NOT update docs that are already accurate
