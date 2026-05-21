---
name: log-first-debugging
description: Instrumentation-first debugging. NEVER guess - add prints at every decision point until exact failure is visible. Use when NameError, ImportError, undefined variable, or mysterious failures.
---

# Log-First Debugging

## Core Principle

**Don't guess. Add logs.**

Guessing wastes hours. Logs find bugs in minutes.

## Workflow

```python
# 1. Add entry/exit logs to every potentially involved file
print('DEBUG: fly/config.py START')
print('DEBUG: fly/config.py END')

# 2. Add logs at every function boundary
def init():
    print('DEBUG: init() START')
    ...
    print('DEBUG: init() END')

# 3. Add logs for imports and values
from fly.config import get_config
print('DEBUG: get_config:', get_config)

# 4. Run and observe
# Last successful log shows exact failure location

# 5. Fix root cause
# 6. Remove debug logs
```

## When to Use

- After 2+ guess-based fixes failed
- Multi-language boundaries (Python ↔ C++)
- Any NameError/ImportError/undefined variable
- "The code should work but doesn't"

## Checklist

```
[ ] START/END log on all potentially involved files
[ ] Entry/exit log on all called functions
[ ] Import/value logs
[ ] Run → last successful log = exact failure location
[ ] Root cause identified from log evidence
[ ] Fix applied → test passes
[ ] Debug logs removed
```
