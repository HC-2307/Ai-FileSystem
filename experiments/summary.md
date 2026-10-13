# Results

Median of 3 runs (seeds [1, 2, 3]). Cache budget 1024 KiB, 16 MiB of file data, 8192 operations per run. Produced by `python/evaluate.py`.


## Think time 0 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 158 | 121 | 81 |
| random | 315 | 300 | 254 |
| repetitive | 303 | 193 | 121 |
| mixed | 242 | 265 | 212 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 431 | 383 | 313 |
| random | 730 | 661 | 594 |
| repetitive | 735 | 567 | 460 |
| mixed | 602 | 685 | 567 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 260 | 253 | 203 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 6,323 | 8,262 | 12,383 |
| random | 3,174 | 3,333 | 3,933 |
| repetitive | 3,299 | 5,190 | 8,233 |
| mixed | 4,103 | 3,788 | 4,736 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0.056 | 0.054 | 0.056 |
| repetitive | 0.046 | 0.807 | 0.860 |
| mixed | 0.039 | 0.454 | 0.269 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.674 | 0.810 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.766 | 0.814 |
| mixed | 0 | 0.422 | 0.236 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,047 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 406 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,735 |
| repetitive | 7,815 | 7,988 | 7,817 |
| mixed | 7,100 | 9,059 | 7,714 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 1,022 |
| random | 1,022 | 1,022 | 1,022 |
| repetitive | 954 | 955 | 954 |
| mixed | 1,022 | 1,022 | 1,022 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 287 |
| random | 5,313 | 6,115 | 5,313 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 3,854 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 106 | 172 | 9.320 |
| random | 116 | 134 | 80 |
| repetitive | 12 | 13 | 9.560 |
| mixed | 83 | 121 | 60 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 1 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 1.000 |
| mixed | 0 | 0 | 0.350 |

## Think time 200 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 435 | 80 | 77 |
| random | 509 | 487 | 460 |
| repetitive | 506 | 158 | 175 |
| mixed | 483 | 331 | 364 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 934 | 218 | 168 |
| random | 1,108 | 1,112 | 965 |
| repetitive | 1,187 | 862 | 1,013 |
| mixed | 1,379 | 1,339 | 1,035 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 527 | 400 | 383 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 2,297 | 12,569 | 12,933 |
| random | 1,965 | 2,054 | 2,176 |
| repetitive | 1,976 | 6,313 | 5,713 |
| mixed | 2,050 | 2,965 | 2,731 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0.056 | 0.054 | 0.056 |
| repetitive | 0.046 | 0.812 | 0.860 |
| mixed | 0.039 | 0.457 | 0.279 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.675 | 0.812 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.771 | 0.814 |
| mixed | 0 | 0.424 | 0.240 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 420 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,735 |
| repetitive | 7,815 | 7,988 | 7,818 |
| mixed | 7,100 | 9,062 | 7,739 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 1,022 |
| random | 1,022 | 1,022 | 1,022 |
| repetitive | 954 | 955 | 954 |
| mixed | 1,022 | 1,022 | 1,022 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 283 |
| random | 5,313 | 6,115 | 5,313 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 3,863 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 249 | 135 | 8.900 |
| random | 183 | 195 | 148 |
| repetitive | 18 | 14 | 16 |
| mixed | 136 | 142 | 103 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 1 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 1.000 |
| mixed | 0 | 0 | 0.377 |
