# Results

Median of 3 runs (seeds [1, 2, 3]). Cache budget 1024 KiB, 16 MiB of file data, 8192 operations per run. Produced by `python/evaluate.py`.


## Think time 0 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 162 | 120 | 64 |
| random | 309 | 290 | 293 |
| repetitive | 281 | 191 | 139 |
| mixed | 255 | 262 | 197 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 427 | 362 | 286 |
| random | 820 | 782 | 808 |
| repetitive | 665 | 506 | 559 |
| mixed | 769 | 646 | 555 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 261 | 247 | 226 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 6,180 | 8,335 | 15,656 |
| random | 3,238 | 3,446 | 3,412 |
| repetitive | 3,558 | 5,227 | 7,190 |
| mixed | 3,911 | 3,836 | 4,999 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0.056 | 0.054 | 0.053 |
| repetitive | 0.046 | 0.808 | 0.831 |
| mixed | 0.039 | 0.454 | 0.538 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 0.860 |
| mixed | 0 | 0.674 | 0.952 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.767 | 0.792 |
| mixed | 0 | 0.422 | 0.508 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 1,067 |
| mixed | 0 | 1,510 | 188 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,761 |
| repetitive | 7,815 | 7,988 | 8,956 |
| mixed | 7,100 | 9,058 | 7,736 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 1,022 |
| random | 1,022 | 1,022 | 969 |
| repetitive | 954 | 955 | 1,006 |
| mixed | 1,022 | 1,022 | 1,009 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 297 |
| random | 5,313 | 6,115 | 4,731 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 1,981 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 107 | 172 | 8.350 |
| random | 108 | 117 | 93 |
| repetitive | 9 | 9.460 | 13 |
| mixed | 84 | 116 | 39 |

## Think time 200 us between operations


**Read latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 379 | 95 | 74 |
| random | 484 | 422 | 499 |
| repetitive | 498 | 155 | 155 |
| mixed | 395 | 323 | 252 |

**Read p99 (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,021 | 242 | 244 |
| random | 1,641 | 1,002 | 1,097 |
| repetitive | 1,149 | 757 | 822 |
| mixed | 1,002 | 1,394 | 924 |

**Write latency (us)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 0 | 0 |
| repetitive | 0 | 0 | 0 |
| mixed | 436 | 376 | 357 |

**Throughput (ops/s)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 2,639 | 10,578 | 13,437 |
| random | 2,065 | 2,367 | 2,005 |
| repetitive | 2,008 | 6,444 | 6,442 |
| mixed | 2,506 | 3,035 | 3,786 |

**Cache hit rate**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0.056 | 0.054 | 0.053 |
| repetitive | 0.046 | 0.812 | 0.833 |
| mixed | 0.039 | 0.456 | 0.543 |

**Prefetch precision**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 1 | 1 |
| random | 0 | 0.043 | 0 |
| repetitive | 0 | 0.974 | 0.859 |
| mixed | 0 | 0.675 | 0.952 |

**Reads served by prefetch**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0.997 | 0.997 |
| random | 0 | 0.011 | 0 |
| repetitive | 0 | 0.771 | 0.795 |
| mixed | 0 | 0.424 | 0.510 |

**Wasted prefetches**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 0 | 0 | 0 |
| random | 0 | 2,048 | 0 |
| repetitive | 0 | 169 | 1,076 |
| mixed | 0 | 1,510 | 188 |

**Disk reads**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,761 |
| repetitive | 7,815 | 7,988 | 8,970 |
| mixed | 7,100 | 9,062 | 7,739 |

**Memory (KiB)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 1,022 | 1,022 | 1,022 |
| random | 1,022 | 1,022 | 996 |
| repetitive | 954 | 955 | 1,006 |
| mixed | 1,022 | 1,022 | 1,009 |

**Mapping queries**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,225 | 297 |
| random | 5,313 | 6,115 | 4,731 |
| repetitive | 559 | 560 | 559 |
| mixed | 4,660 | 5,086 | 1,981 |

**Mapping time (ms)**

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 197 | 143 | 9.160 |
| random | 151 | 174 | 145 |
| repetitive | 15 | 14 | 14 |
| mixed | 112 | 134 | 59 |
