# Section Skip Performance Test Report

## Test Configuration
- **Test File**: Galaxy_large.def (925MB)
- **Test Method**: Parse NETS and SPECIALNETS only, with empty callbacks
- **Runs per test**: 5

## Test Results

### 6.0 Version - Full Parse (No Skip)
| Run | Time (s) |
|-----|----------|
| 1 | 19.74 |
| 2 | 16.84 |
| 3 | 16.58 |
| 4 | 16.98 |
| 5 | 17.08 |
| **Avg** | **17.44s** |

### 6.1 Version - Full Parse (No Skip)
| Run | Time (s) |
|-----|----------|
| 1 | 14.11 |
| 2 | 12.70 |
| 3 | 12.86 |
| 4 | 12.83 |
| 5 | 12.82 |
| **Avg** | **13.06s** |

### 6.1 Version - Skip COMPONENTS
| Run | Time (s) |
|-----|----------|
| 1 | 13.06 |
| 2 | 11.28 |
| 3 | 11.27 |
| 4 | 11.33 |
| 5 | 11.46 |
| **Avg** | **11.68s** |

## Performance Comparison

| Version | Avg Time | vs 6.0 | vs 6.1 No Skip |
|---------|----------|--------|----------------|
| 6.0 Full Parse | 17.44s | baseline | - |
| 6.1 Full Parse | 13.06s | **25.1% faster** | baseline |
| **6.1 Skip COMPONENTS** | **11.68s** | **33.0% faster** | **10.6% faster** |

## Analysis

### Performance Gains

1. **6.1 vs 6.0**: 25.1% improvement
   - Due to buffer size optimization (16KB → 256KB)
   - IncCurPos inline optimization
   - GPERF keyword lookup

2. **Skip COMPONENTS vs Full Parse**: 10.6% additional improvement
   - Skips ~909,926 component entries
   - Eliminates memory allocation for defiComponent objects
   - Reduces parser state transitions

### COMPONENTS Section Impact

| Section | Lines | % of File |
|---------|-------|-----------|
| COMPONENTS | ~909,926 | ~10% |
| NETS | ~4,952,560 | ~54% |
| SPECIALNETS | ~3,338,083 | ~36% |

**Key Insight**: COMPONENTS section accounts for ~10% of file but skipping provides 10.6% performance gain - proportional to its size.

## API Usage

```cpp
// Parse NETS and SPECIALNETS only, skip COMPONENTS
defrSetSkipComponents(1);
defrSetNetCbk(myNetCbk);
defrSetSNetCbk(mySNetCbk);
defrRead(file, filename, userData, 1);

// Parse NETS only, skip COMPONENTS and SPECIALNETS
defrSetSkipComponents(1);
defrSetSkipSpecialNets(1);
defrSetNetCbk(myNetCbk);
defrRead(file, filename, userData, 1);

// Parse SPECIALNETS only, skip COMPONENTS and NETS
defrSetSkipComponents(1);
defrSetSkipNets(1);
defrSetSNetCbk(mySNetCbk);
defrRead(file, filename, userData, 1);
```

## Conclusion

The Section Skip feature provides:
1. **Significant performance gain**: 10.6% improvement when skipping COMPONENTS
2. **Flexible API**: Users can choose which sections to parse
3. **gz compatibility**: Works with both regular and gzipped DEF files
4. **Combined benefit**: 33% total improvement over 6.0 baseline

---
Generated: 2026-04-01
Test file: Galaxy_large.def (925MB)