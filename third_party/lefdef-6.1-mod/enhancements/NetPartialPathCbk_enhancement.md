# LEFDEF 6.1 Enhancement: NetPartialPathCbk for Normal NETS

## Overview

This enhancement implements `defrSetNetPartialPathCbk` functionality for normal NETS, 
allowing users to process net wiring data in batches to reduce peak memory usage.

## Problem Statement

In production environments, parsing NETS section with large amounts of wiring data 
causes significant memory peaks:
- LEFDEF 5.8: Peak memory ~6GB for NETS parsing
- LEFDEF 6.1: Peak memory ~20GB for NETS parsing (without this enhancement)

## Solution

Add `defrSetNetPartialPathCbk` callback for normal NETS, similar to existing 
`defrSetSNetPartialPathCbk` for SPECIALNETS. The callback triggers when Wire path 
count reaches allocation limit, allowing users to process data incrementally.

## API Changes

### New Enum Value

```cpp
// defrReader.hpp, defrReader.h
defrNetPartialPathCbkType = 70
```

### Existing API (Now Functional)

```cpp
// defrReader.cpp - already defined, now functional
void defrSetNetPartialPathCbk(defrNetCbkFnType f);
void defrUnsetNetPartialPathCbk();
```

## Modified Files

| File | Changes |
|------|---------|
| `def/def/defrReader.hpp` | Added `defrNetPartialPathCbkType` enum |
| `def/include/defrReader.h` | Added enum with value 70 |
| `def/cdef/defrReader.h` | Added enum with value 70 |
| `def/def/def.y` | Added callback trigger in `paths` and `new_path` rules |
| `def/def/def.tab.cpp` | Auto-generated from def.y |

## Grammar Changes

### paths Rule (Before)

```yacc
paths:
    path   // not necessary to do partial callback for net yet
    {
        if (defData->callbacks->NetCbk) {
            defData->finishPath(0, &defData->needNPCbk);
            defData->PathObj = NULL;
        }
    }
```

### paths Rule (After)

```yacc
paths:
    path
    {
        if (defData->callbacks->NetCbk) {
            if (defData->needNPCbk && defData->callbacks->NetPartialPathCbk) {
                CALLBACK(defData->callbacks->NetPartialPathCbk, 
                         defrNetPartialPathCbkType, defData->Net);
                defData->needNPCbk = 0;
                defData->finishPath(1, &defData->needNPCbk);
                defData->Net->clearRectPolyNPath();
            } else {
                defData->finishPath(0, &defData->needNPCbk);
            }
            defData->PathObj = NULL;
        }
    }
```

## Usage Example

### Basic Usage

```cpp
#include "defrReader.hpp"

static int partial_count = 0;
static long total_paths = 0;

int partialPathCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    partial_count++;
    
    // Process Wire paths incrementally
    for (int w = 0; w < net->numWires(); w++) {
        defiWire* wire = net->wire(w);
        for (int p = 0; p < wire->numPaths(); p++) {
            defiPath* path = wire->path(p);
            
            // Process path data...
            path->initTraverse();
            int elemType;
            while ((elemType = path->next()) != DEFIPATH_DONE) {
                switch (elemType) {
                    case DEFIPATH_LAYER:
                        const char* layer = path->getLayer();
                        break;
                    case DEFIPATH_POINT:
                        int x, y;
                        path->getPoint(&x, &y);
                        break;
                    // ...
                }
            }
            total_paths++;
        }
    }
    
    // Data will be cleared after callback returns
    return 0;
}

int netCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    // Process remaining paths (< batch size)
    for (int w = 0; w < net->numWires(); w++) {
        defiWire* wire = net->wire(w);
        // ... process remaining paths
    }
    return 0;
}

int main() {
    defrInit();
    
    // Register callbacks
    defrSetNetCbk(netCallback);
    defrSetNetPartialPathCbk(partialPathCallback);
    
    // Parse DEF file
    FILE* f = fopen("design.def", "r");
    defrRead(f, "design.def", NULL, 0);
    fclose(f);
    
    printf("Partial callbacks: %d\n", partial_count);
    printf("Total paths: %ld\n", total_paths);
    
    defrClear();
    return 0;
}
```

## Memory Behavior

| Scenario | Without Enhancement | With Enhancement |
|----------|--------------------|--------------------|
| 100 paths in net | All accumulated | 8 paths per batch |
| 1000 paths in net | Peak ~all paths | Peak ~8 paths |
| 10000 paths in net | Peak ~10000 paths | Peak ~8 paths (stable) |

## Trigger Threshold

Default trigger threshold is based on Wire initial allocation:
- Normal NETS: 8 paths (trigger when paths == 8)
- SPECIALNETS: 1000 paths (trigger when paths == 1000)

To change threshold, modify `defiNet.cpp`:
```cpp
// defiNet.cpp:802-811
switch (netOsnet) {
  case 2:  // SPECIALNETS
    bumpPaths(pathsAllocated_ ? incNumber : 1000);  // Change 1000
    break;
  default: // Normal NETS
    bumpPaths(pathsAllocated_ ? incNumber : 8);     // Change 8
    break;
}
```

## Emulating LEFDEF 5.8 PathCbk

To emulate 5.8's per-path callback behavior without modifying parser code:

```cpp
static int processed_paths = 0;

int partialPathCallback(defrCallbackType_e type, defiNet* net, void*) {
    for (int w = 0; w < net->numWires(); w++) {
        defiWire* wire = net->wire(w);
        int num_paths = wire->numPaths();
        
        // Only process unprocessed paths (incremental processing)
        for (int p = processed_paths; p < num_paths; p++) {
            defiPath* path = wire->path(p);
            
            // Same access pattern as 5.8 PathCbk
            path->initTraverse();
            int type;
            while ((type = path->next()) != DEFIPATH_DONE) {
                // Process elements in same order as 5.8
            }
            processed_paths++;
        }
    }
    
    // Reset counter (Wire paths cleared after callback)
    processed_paths = 0;
    
    return 0;
}
```

## Testing

Test program located in `def/test_partial_path/`:
- `test_net_partial_path.cpp`: Basic functionality test
- `test_emulate_58_no_mod.cpp`: 5.8 PathCbk emulation test

### Test Results

```
Testing with 100 paths in normal NET:
- Partial callbacks: 12 (triggered every 8 paths)
- Net callback: 1 (final)
- Peak memory: 3.4 MB (stable)
- Total paths processed: 96 + 4 = 100
```

## Comparison with SNetPartialPathCbk

| Feature | NetPartialPathCbk | SNetPartialPathCbk |
|---------|-------------------|---------------------|
| Section | NETS | SPECIALNETS |
| Trigger threshold | 8 paths | 1000 paths |
| Data types | Wire paths only | Wire paths + Polygon + Rect + Via |
| Clear function | `clearRectPolyNPath()` | `clearRectPolyNPath()` + `clearVia()` |
| API | `defrSetNetPartialPathCbk` | `defrSetSNetPartialPathCbk` |

## Performance Impact

- Memory: Significantly reduced peak for nets with many paths
- CPU: Minimal overhead (callback only when threshold reached)
- Compatibility: Fully compatible with existing code

## Installation

1. Apply patch:
```bash
cd lefdef6.0
patch -p1 < NetPartialPathCbk_enhancement.patch
```

2. Rebuild:
```bash
cd def/def
bison -v -pdefyy -d def.y
mv def.tab.c def.tab.cpp
# Rebuild library...
```

