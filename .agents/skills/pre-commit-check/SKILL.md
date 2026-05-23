---
name: pre-commit-check
description: Pre-commit gate that analyzes code changes, checks documentation consistency (existing accuracy + new behavior coverage), presents diff summary to user for confirmation, updates docs if needed, then commits. Automatically triggers when user says 'commit', '提交', 'push', or '推送'.
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

For **every commit with code changes**, apply TWO checks:

#### Check A: Existing Docs Accuracy (for API/Behavior/Internal changes)

Scan **all `.md` files under `docs/`** AND `CLAUDE.md` for references to changed code. For each changed file/function/path:

- [ ] Grep `docs/` and `CLAUDE.md` for old paths, old function names, old class names that were moved/renamed/removed
- [ ] Verify every hit is still accurate — if not, flag for update
- [ ] `CLAUDE.md` — module tables, API sections, architecture flows still accurate?
- [ ] `docs/python-api/module.md` — Python-exposed methods match exports?
- [ ] `docs/architecture.md` + `docs/architecture/overview.md` — diagrams, flows, imports match?

**When files are moved/renamed** (git diff shows `rename from` / `rename to` or path changes):
- [ ] Grep ALL docs for the **old path** — every reference must be updated or annotated
- [ ] Check `docs/NEW_MODULE_GUIDE.md` — does it reference the correct current paths?
- [ ] Check `docs/DEVELOPMENT_GUIDELINES.md` — are build commands and conventions current?
- [ ] Check `docs/<affected_module>/module.md` — file tables accurate?

#### Check B: New Behavior Documentation (MANDATORY)

Any **new feature, new behavior, or changed behavior** MUST be reflected in documentation — even if no existing doc mentioned it before. This includes:

- New public API or user-facing behavior (e.g., path auto-increment in `open_db`)
- New generation algorithms or changed defaults (e.g., UUID v4 replacing hash)
- New lifecycle or cleanup semantics (e.g., destructor unregister, strict register checks)
- New error conditions or recovery paths

**Where to document**:
- User-facing behavior → `docs/python-api/module.md` or relevant module doc
- Internal behavior changes → `docs/<module>/module.md`
- Architecture/archival → `CLAUDE.md`
- All doc changes → `docs/DOC_CHANGELOG.md`

**The rule: "Not mentioned in docs before" is NOT a reason to skip documentation. New behavior always requires documentation.**

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
- [x] docs/ grep for old paths — [N stale references found / clean]
- [x] docs/NEW_MODULE_GUIDE.md — [up to date / needs update: reason]
- [x] docs/DEVELOPMENT_GUIDELINES.md — [up to date / needs update: reason]
- [x] docs/architecture*.md — [up to date / needs update: reason]
- [x] docs/<module>/module.md — [up to date / needs update: reason]

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

## Fast Path: Non-Code Changes

When ALL changed files are **none** of the following:
- Source code (`src/` under any language: `.cpp`, `.h`, `.py`, `.java`, etc.)
- Tests (`tests/`, `qa/`, `*_test.*`, `test_*`)
- Build system (`BUILD`, `BUILD.bazel`, `.bazelrc`, `WORKSPACE`, `MODULE.bazel`, `CMakeLists.txt`, `Makefile`)
- Documentation that mirrors code (`CLAUDE.md`, `docs/`)

Then:
1. Skip Step 2–4 entirely (no classification, no doc check, no summary confirmation)
2. Stage, commit, and push immediately
3. Print: `Fast-committed [message] and pushed.`

Examples of fast-path changes: `.agents/skills/`, `.opencode/`, `.github/`, `.gitignore`, `LICENSE`, `README.md` (non-technical), config dotfiles, etc.

## Rules

- NEVER commit without user confirmation — **except** on fast path
- NEVER skip the documentation check — **except** on fast path
- Match existing commit message style from git log
- If changes span multiple concerns → suggest splitting into multiple commits
- Do NOT update docs that are already accurate
