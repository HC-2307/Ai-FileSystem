# Results

Median of 3 runs (seeds [1, 2, 3]). Cache budget 1024 KiB, 16 MiB of file data, 8192 operations per run. Produced by `python/evaluate.py`.


## Think time 0 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 148 | 116 | 87 |
| random | 329 | 321 | 282 |
| repetitive | 347 | 195 | 125 |
| mixed | 252 | 275 | 212 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 391 | 355 | 347 |
| random | 745 | 718 | 623 |
| repetitive | 844 | 567 | 507 |
| mixed | 676 | 699 | 568 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 263 | 262 | 200 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 6,776 | 8,601 | 11,479 |
| random | 3,042 | 3,114 | 3,542 |
| repetitive | 2,882 | 5,116 | 7,996 |
| mixed | 3,952 | 3,653 | 4,743 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.996 |
| random | 0.056 | 0.054 | 0.042 |
| repetitive | 0.046 | 0.809 | 0.860 |
| mixed | 0.039 | 0.453 | 0.262 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.673 | 0.810 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.996 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.768 | 0.825 |
| mixed | 0 | 0.421 | 0.236 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,047 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 422 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,845 |
| repetitive | 7,815 | 7,988 | 7,908 |
| mixed | 7,100 | 9,062 | 7,816 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 1,023 |
| random | 1,022 | 1,022 | 708 |
| repetitive | 954 | 955 | 546 |
| mixed | 1,022 | 1,022 | 825 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 287 |
| random | 5,313 | 6,115 | 4,725 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 2,443 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 105 | 165 | 9.270 |
| random | 128 | 138 | 77 |
| repetitive | 13 | 12 | 9.290 |
| mixed | 86 | 125 | 40 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 1 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0.998 |
| mixed | 0 | 0 | 0.351 |

## Think time 200 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 414 | 81 | 76 |
| random | 543 | 547 | 469 |
| repetitive | 524 | 178 | 139 |
| mixed | 497 | 333 | 351 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 926 | 241 | 158 |
| random | 1,197 | 1,231 | 1,066 |
| repetitive | 1,225 | 845 | 775 |
| mixed | 1,104 | 1,009 | 1,023 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 556 | 398 | 370 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 2,413 | 12,294 | 13,074 |
| random | 1,840 | 1,827 | 2,134 |
| repetitive | 1,908 | 5,622 | 7,178 |
| mixed | 1,991 | 2,945 | 2,833 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0.056 | 0.054 | 0.037 |
| repetitive | 0.046 | 0.812 | 0.860 |
| mixed | 0.039 | 0.457 | 0.268 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 1 |
| mixed | 0 | 0.675 | 0.805 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.771 | 0.813 |
| mixed | 0 | 0.424 | 0.238 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 0 |
| mixed | 0 | 1,510 | 439 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,890 |
| repetitive | 7,815 | 7,988 | 7,813 |
| mixed | 7,100 | 9,063 | 7,838 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 770 |
| random | 1,022 | 1,022 | 728 |
| repetitive | 954 | 955 | 1,006 |
| mixed | 1,022 | 1,022 | 834 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 176 |
| random | 5,313 | 6,115 | 3,785 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 2,112 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 229 | 135 | 5.220 |
| random | 201 | 254 | 112 |
| repetitive | 20 | 21 | 14 |
| mixed | 157 | 154 | 66 |

**Model accuracy (C only)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 1 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 1.000 |
| mixed | 0 | 0 | 0.377 |
