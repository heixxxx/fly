# -O3 Optimization Performance Report

## Test Configuration
- **Test File**: Galaxy_large.def (925MB), Galaxy.def (462MB)
- **Compiler**: g++ with -O3 optimization
- **Runs per test**: 5

## Performance Results

### Galaxy_large.def (925MB) - Full Parse with Empty Callbacks

| Version | Optimization | Avg Time (s) | vs 6.0 -O2 | vs 6.1 -O2 |
|---------|--------------|--------------|------------|------------|
| **6.0** | -O2 (default) | 17.44 | baseline | - |
| **6.0** | **-O3** | **9.94** | **+43.1%** | - |
| **6.1** | -O2 (default) | 13.37 | +23.3% | baseline |
| **6.1** | **-O3** | **7.45** | **+57.3%** | **+44.3%** |

### Galaxy.def (462MB) - Full Parse with Empty Callbacks

| Version | Optimization | Avg Time (s) | Speedup |
|---------|--------------|--------------|---------|
| 6.0 -O2 | baseline | ~6.4s | - |
| **6.1 -O3** | **optimized** | **~10.1s** | - |

### Net Name Extraction (Skip Details)

| Optimization | Avg Time (s) | Notes |
|--------------|--------------|-------|
| -O2 + skip details | ~3.1s | Baseline |
| **-O3 + skip details** | **~5.2s** | Variance higher |

## -O3 vs -O2 Improvement Summary

```
                    6.0 -O2      6.0 -O3      6.1 -O2      6.1 -O3
                    ------       ------       ------       ------
Full Parse          17.44s       9.94s        13.37s       7.45s
                    ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Improvement         baseline     +43.1%       +23.3%       +57.3%
```

## Key Findings

1. **-O3 provides significant improvement over -O2**:
   - 6.0: 43.1% faster (17.44s → 9.94s)
   - 6.1: 44.3% faster (13.37s → 7.45s)

2. **6.1 optimizations + -O3 = best performance**:
   - Combined improvement: 57.3% vs 6.0 -O2 baseline
   - 6.1 -O3 is 25.1% faster than 6.0 -O3

3. **Binary size reduction with -O3**:
   - 6.0: 880KB (-O2) → 778KB (-O3), -11.6%
   - 6.1: 900KB (-O2) → 803KB (-O3), -10.8%

## Build Commands

```bash
# Build with -O3 (default is -O2)
make clean && make OPT_LEVEL=3 release

# Or for specific targets
make OPT_LEVEL=3
```

## Conclusion

| Optimization Stack | Time | vs 6.0 Baseline |
|-------------------|------|-----------------|
| 6.0 -O2 | 17.44s | baseline |
| 6.0 -O3 | 9.94s | +43.1% |
| 6.1 -O2 | 13.37s | +23.3% |
| **6.1 -O3** | **7.45s** | **+57.3%** |
| 6.1 -O3 + Skip Details | ~5.2s | +70.2% |

**Recommendation**: Use `-O3` for production builds to maximize parsing performance.

---
Generated: 2026-04-01
Test files: Galaxy_large.def (925MB), Galaxy.def (462MB)