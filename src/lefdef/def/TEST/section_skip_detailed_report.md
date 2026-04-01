# Section Skip Performance Test Report

## Test Configuration
- **Test File**: Galaxy_large.def (925MB)
- **Components**: ~909,926 entries
- **Nets**: ~495,256 entries (2x original)
- **Special Nets**: ~85,006 entries (2x original)
- **Runs per test**: 5

## Performance Results

### Version Comparison

| Version | Test Case | Avg Time (s) | vs 6.0 Baseline |
|---------|-----------|--------------|-----------------|
| **6.0** | Full Parse (NetCbk + SNetCbk) | **17.03** | baseline |
| **6.1** | Full Parse (NetCbk + SNetCbk) | **13.37** | **21.5% faster** |
| **6.1** | Full Parse (NetNameCbk + SNetCbk) | **11.23** | **34.1% faster** |
| **6.1** | Skip COMPONENTS (NetCbk + SNetCbk) | **11.68** | **31.4% faster** |

### NetNameCbk vs NetCbk Comparison

| Test Scenario | NetCbk (s) | NetNameCbk (s) | Improvement |
|---------------|------------|----------------|-------------|
| Full parse (no skip) | 13.37 | **11.23** | **16.0% faster** |
| Skip COMPONENTS + SNETS | 9.56 | **7.83** | **18.1% faster** |

**Key Finding**: NetNameCbk is consistently ~16-18% faster than NetCbk, regardless of skip settings.

### Section Skip Tests (6.1)

| Test Case | Skip Flags | Callback | Avg Time (s) | vs 6.1 Full | Speedup |
|-----------|------------|----------|--------------|-------------|---------|
| Full Parse | - | NetCbk | 13.37 | baseline | - |
| Full Parse | - | NetNameCbk | **11.23** | **16.0% faster** | Light callback |
| Skip COMPONENTS | `-skipcomp` | NetCbk | 11.68 | 12.6% faster | Skip ~909K entries |
| NETS only | `-skipcomp -skipsnets` | NetCbk | 9.56 | 28.5% faster | Skip ~1.8M lines |
| NETS only | `-skipcomp -skipsnets` | NetNameCbk | **7.83** | **41.4% faster** | Light callback |
| SNETS only | `-skipcomp -skipnets` | SNetCbk | **5.85** | **56.2% faster** | Skip ~5.3M lines |

### 6.0 vs 6.1 Comparison

```
                        6.0         6.1 Full      6.1 NetName    6.1 SkipComp    6.1 NETS only
                        ----        ---------     -----------    -----------    ------------
NetCbk + SNetCbk        17.03s      13.37s        -              11.68s         9.56s
NetNameCbk + SNetCbk    -           11.23s        -              -              7.83s
SNetCbk only            -           -             -              5.85s          -

Performance vs 6.0:     0%          +21.5%        +34.1%         +31.4%         +43.9%
```

## Analysis

### Time Distribution by Section

| Section | Lines | % of File | Skip Time (s) | % of Parse Time |
|---------|-------|-----------|---------------|-----------------|
| COMPONENTS | ~909,926 | ~10% | ~1.69s | ~12.6% |
| NETS | ~4,952,560 | ~54% | ~7.81s | ~58.4% |
| SPECIALNETS | ~3,338,083 | ~36% | ~3.51s | ~26.2% |
| **Total Skip Time** | - | - | **13.01s** | ~97.2% |

*Note: Remaining ~2.8% is file header and other sections*

### NetNameCbk vs NetCbk

| Callback | Scenario | Time (s) | Diff | Explanation |
|----------|----------|----------|------|-------------|
| NetCbk | Full parse | 13.37 | baseline | Full defiNet object construction |
| NetNameCbk | Full parse | 11.23 | **16.0% faster** | Only net name string, no object |
| NetCbk | Skip Comp + SNETS | 9.56 | baseline | Full defiNet object construction |
| NetNameCbk | Skip Comp + SNETS | 7.83 | **18.1% faster** | Only net name string, no object |

**Insight**: NetNameCbk is consistently faster because:
1. No defiNet object allocation
2. No pin/wire/route data parsing
3. Only string copy for net name
4. Triggered early in lexer (at net name token)

**When to use NetNameCbk**:
- Only need net names (e.g., net name validation, name conflict detection)
- Building net name index
- Quick scan for specific net names

## Use Case Recommendations

### Scenario 1: Extract Net Names Only
```cpp
defrSetSkipComponents(1);
defrSetSkipSpecialNets(1);
defrSetNetNameCbk(myNetNameCbk);
defrRead(file, filename, userData, 1);
```
**Performance**: 7.83s (54.0% faster than 6.0 full parse)

### Scenario 2: Parse All Nets (Full Data)
```cpp
defrSetSkipComponents(1);
defrSetSkipSpecialNets(1);
defrSetNetCbk(myNetCbk);
defrRead(file, filename, userData, 1);
```
**Performance**: 9.56s (43.9% faster than 6.0 full parse)

### Scenario 3: Parse Special Nets Only
```cpp
defrSetSkipComponents(1);
defrSetSkipNets(1);
defrSetSNetCbk(mySNetCbk);
defrRead(file, filename, userData, 1);
```
**Performance**: 5.85s (65.7% faster than 6.0 full parse)

### Scenario 4: Parse Both NETS and SPECIALNETS
```cpp
defrSetSkipComponents(1);
defrSetNetCbk(myNetCbk);
defrSetSNetCbk(mySNetCbk);
defrRead(file, filename, userData, 1);
```
**Performance**: 11.68s (31.4% faster than 6.0 full parse)

## API Reference

```cpp
// Skip flags - set before defrRead()
void defrSetSkipComponents(int skip);    // Skip COMPONENTS section
void defrSetSkipNets(int skip);          // Skip NETS section
void defrSetSkipSpecialNets(int skip);   // Skip SPECIALNETS section

// Light callback for net names only
void defrSetNetNameCbk(defrStringCbkFnType cbk);
```

## Conclusion

1. **Section Skip provides significant performance gains**:
   - Skip COMPONENTS: 12.6% improvement
   - Skip COMPONENTS + SPECIALNETS (NETS only): 28.5% improvement
   - Skip COMPONENTS + NETS (SNETS only): 56.2% improvement

2. **NetNameCbk offers consistent optimization (~16-18%)**:
   - Full parse (no skip): 16.0% faster than NetCbk
   - With skip options: 18.1% faster than NetCbk
   - Lower memory footprint (no defiNet objects)
   - **Works without skip flags - can be used in any scenario**

3. **Combined optimization (6.0 → 6.1)**:
   | Optimization | Time (s) | vs 6.0 |
   |--------------|----------|--------|
   | 6.0 Full Parse | 17.03 | baseline |
   | 6.1 Full Parse | 13.37 | +21.5% |
   | 6.1 + NetNameCbk | 11.23 | +34.1% |
   | 6.1 + SkipComp | 11.68 | +31.4% |
   | 6.1 + SkipComp + SkipSNETS + NetNameCbk | 7.83 | +54.0% |
   | 6.1 + SkipComp + SkipNETS | 5.85 | +65.7% |

4. **Best practices**:
   - For net name extraction: Use `NetNameCbk` (16-18% faster, works standalone)
   - For selective section parsing: Combine `NetNameCbk` + skip flags
   - Maximum improvement: **65.7%** (skip COMPONENTS + NETS, parse SNETS only)

---
Generated: 2026-04-01
Test file: Galaxy_large.def (925MB)