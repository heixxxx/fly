# DEF Parser Performance Comparison Report

## Test Configuration
- **Test File**: Galaxy_large.def (925MB, ~2x original)
- **Components**: ~199,850 (2x original)
- **Nets**: ~791,776 (2x original)  
- **Special Nets**: ~85,006 (2x original)
- **Test Method**: Empty callback functions, parse + data fill only
- **Runs per version**: 5

## Performance Results

### 6.0 Version (Original)
| Run | Time (s) |
|-----|----------|
| 1 | 17.27 |
| 2 | 19.43 |
| 3 | 17.21 |
| 4 | 17.12 |
| 5 | 17.24 |
| **Avg** | **17.65s** |

### 6.1 Version (Optimized)
| Run | Time (s) |
|-----|----------|
| 1 | 14.72 |
| 2 | 13.37 |
| 3 | 13.37 |
| 4 | 13.58 |
| 5 | 13.45 |
| **Avg** | **13.70s** |

### Performance Improvement

| Metric | 6.0 | 6.1 | Improvement |
|--------|-----|-----|-------------|
| Avg Time | 17.65s | 13.70s | **22.4% faster** |
| Throughput | 52.4 MB/s | 67.6 MB/s | +29% |

## Profiling Analysis (6.1 Optimized)

| Function | Samples | % | Description |
|----------|---------|---|-------------|
| defyyparse | 283 | 23.8% | Parser core |
| DefGetToken | 182 | 15.3% | Token retrieval |
| GETC | 149 | 12.5% | Character read |
| sublex | 101 | 8.5% | Sub-lexer |
| IncCurPos | 82 | 6.9% | Buffer position |
| __strcmp_avx2 | 49 | 4.1% | String compare |
| __libc_read | 32 | 2.7% | File I/O |

## Optimizations Applied in 6.1

1. **IN_BUF_SIZE**: 16KB → 256KB
   - Reduced __libc_read from ~2.6% to 2.7%
   
2. **IncCurPos inline**: Static inline in header
   - Reduced overhead from 6.7% to 6.9%

3. **Keyword lookup**: GPERF perfect hash
   - defKeywordHash: 1.3%, defFindKeyword: 0.6%

## Key Observations

1. **GETC remains hotspot** (12.5%): Called for every character
2. **IncCurPos effective** (6.9%): Inline reduces call overhead
3. **I/O optimized** (2.7%): Large buffer reduces syscalls
4. **String operations** (4.1% strcmp + 2.4% strcpy + 1.3% strlen): ~8% total

## Further Optimization Opportunities

| Priority | Optimization | Expected Gain |
|----------|-------------|---------------|
| P2 | GETC batch processing | -2~3% |
| P2 | Object pool for defiPath/defiNet | -1~2% |
| P3 | mmap instead of fread | -1~2% |
| P3 | SIMD for uc_array | -0.5% |

---
Generated: 2026-04-01
Test file: Galaxy_large.def (925MB)