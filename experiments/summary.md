# Results

Median of 3 runs (seeds [1, 2, 3]). Cache budget 1024 KiB, 16 MiB of file data, 8192 operations per run. Produced by `python/evaluate.py`.


## Think time 0 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 176 | 131 | 163 |
| random | 342 | 293 | 275 |
| repetitive | 314 | 194 | 202 |
| mixed | 243 | 257 | 247 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 517 | 405 | 514 |
| random | 814 | 598 | 503 |
| repetitive | 728 | 527 | 500 |
| mixed | 647 | 617 | 532 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 250 | 242 | 231 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 5,688 | 7,656 | 6,121 |
| random | 2,924 | 3,409 | 3,638 |
| repetitive | 3,181 | 5,161 | 4,957 |
| mixed | 4,107 | 3,920 | 4,071 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.794 |
| random | 0.056 | 0.054 | 0.056 |
| repetitive | 0.046 | 0.808 | 0.748 |
| mixed | 0.039 | 0.454 | 0.205 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.674 | 0.764 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.794 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.766 | 0.697 |
| mixed | 0 | 0.421 | 0.167 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 391 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,735 |
| repetitive | 7,815 | 7,988 | 7,814 |
| mixed | 7,100 | 9,060 | 7,650 |

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
| sequential | 8,192 | 8,225 | 725 |
| random | 5,313 | 6,115 | 5,313 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 4,012 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 115 | 183 | 17 |
| random | 133 | 130 | 84 |
| repetitive | 13 | 12 | 8.870 |
| mixed | 80 | 115 | 69 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0.755 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0.787 |
| mixed | 0 | 0 | 0.279 |

## Think time 200 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 408 | 79 | 86 |
| random | 510 | 501 | 359 |
| repetitive | 503 | 156 | 129 |
| mixed | 508 | 333 | 288 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,075 | 205 | 334 |
| random | 1,079 | 1,077 | 702 |
| repetitive | 1,070 | 765 | 516 |
| mixed | 1,082 | 1,005 | 734 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 569 | 399 | 296 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 2,453 | 12,614 | 11,598 |
| random | 1,961 | 1,997 | 2,788 |
| repetitive | 1,989 | 6,425 | 7,773 |
| mixed | 1,944 | 2,946 | 3,458 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.968 |
| random | 0.056 | 0.054 | 0.056 |
| repetitive | 0.046 | 0.812 | 0.833 |
| mixed | 0.039 | 0.457 | 0.254 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.675 | 0.797 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.968 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.771 | 0.786 |
| mixed | 0 | 0.424 | 0.217 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 415 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,193 | 8,192 |
| random | 7,735 | 9,919 | 7,735 |
| repetitive | 7,815 | 7,990 | 7,817 |
| mixed | 7,100 | 9,062 | 7,719 |

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
| sequential | 8,192 | 8,225 | 326 |
| random | 5,313 | 6,115 | 5,313 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 3,845 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 226 | 136 | 9.130 |
| random | 180 | 232 | 102 |
| repetitive | 18 | 19 | 10 |
| mixed | 162 | 151 | 81 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0.992 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0.993 |
| mixed | 0 | 0 | 0.333 |
