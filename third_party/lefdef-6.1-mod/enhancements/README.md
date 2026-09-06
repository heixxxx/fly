# LEFDEF 6.1 Enhancement Package

This directory contains enhancement packages for LEFDEF 6.1 parser.

## Contents

### NetPartialPathCbk Enhancement

**Purpose**: Reduce peak memory usage when parsing NETS with large amounts of wiring data.

**Files**:
- `NetPartialPathCbk_enhancement.md` - Detailed documentation
- `NetPartialPathCbk_enhancement.patch` - Patch file (apply to LEFDEF 6.0)
- `example_usage.cpp` - Basic usage example
- `example_emulate_5.8.cpp` - Example for emulating LEFDEF 5.8 PathCbk

**Key Changes**:
- Add `defrNetPartialPathCbkType` enum (value 70)
- Implement callback trigger in `paths` and `new_path` grammar rules
- Trigger when Wire path count reaches allocation limit
- Clear Wire paths after callback to reduce memory

### All Enhancements Bundle

- `lefdef6.1_all_enhancements.patch` - All enhancements from LEFDEF 6.0

## Previous Enhancements (in git history)

1. **Keyword lookup optimization with gperf** (commit 8269928)
   - Add gperf-based keyword lookup for faster parsing

2. **Section skip optimization** (commit 1e8e16e)
   - Add section skip API for faster selective parsing

3. **Net body skip API** (commit 3f2c06e)
   - Add `defrSetSkipNetDetails` for fast net name extraction
   - ~67% performance improvement

4. **ProcessEscapeInTString option** (commit a39650f)
   - Add escape processing option for T_STRING tokens

## Installation

### Single Enhancement

```bash
cd LEFDEF_6.0_62-p004
patch -p1 < enhancements/NetPartialPathCbk_enhancement.patch

cd def/def
bison -v -pdefyy -d def.y
mv def.tab.c def.tab.cpp

# Rebuild library
g++ -O2 -fPIC -I. -I.. -c def.tab.cpp -o def.tab.o
# ... compile other files
ar rcs libdef.a def.tab.o <other_objects>
```

### All Enhancements

```bash
cd LEFDEF_6.0_62-p004
patch -p1 < enhancements/lefdef6.1_all_enhancements.patch

# Rebuild as above
```

## Usage

### Basic

```cpp
defrInit();
defrSetNetCbk(netCallback);
defrSetNetPartialPathCbk(partialPathCallback);  // New API

FILE* f = fopen("design.def", "r");
defrRead(f, "design.def", NULL, 0);
fclose(f);

defrClear();
```

### Emulating 5.8 PathCbk

See `example_emulate_5.8.cpp` for detailed implementation.

## Testing

Test programs are in `def/test_partial_path/`:
- Compile: `g++ -O2 -o test test.cpp -I../def -L../def -ldef`
- Run: `./test design.def`

## Memory Improvement

| Scenario | Without Enhancement | With Enhancement |
|----------|--------------------|--------------------|
| Net with 1000 paths | Peak: ~1000 paths | Peak: ~8 paths |
| Net with 10000 paths | Peak: ~10000 paths | Peak: ~8 paths (stable) |

## Compatibility

- Fully compatible with existing code
- No API changes (existing `defrSetNetPartialPathCbk` becomes functional)
- Works alongside `defrSetSNetPartialPathCbk` for SPECIALNETS

