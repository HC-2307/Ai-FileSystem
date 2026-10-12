# Evaluation

All numbers here were measured on one machine by `python/evaluate.py`. The raw
rows are in [experiments/results.csv](../experiments/results.csv) and every
metric is tabulated in [experiments/summary.md](../experiments/summary.md).

## Summary

- **Sequential, repetitive and mixed workloads:** prediction (B) cuts average read latency against the baseline (A) in five of six cases, and the adaptive version (C) is as fast as B or faster in all six.
- **Random workload:** no version is meaningfully faster. C's contribution is that it stops prefetching: B wastes about 2,000 prefetches per run, C wastes none.
- **Where C is worse than B:** on the repetitive workload C wastes about six times as many prefetches as B (1,067 against 169) and reads about 12% more blocks from the image, because feedback pushes it to prefetch 8 blocks deep.

## Setup

| | |
|---|---|
| Data | 8 files of 512 blocks (16 MiB) |
| Cache budget | 1024 KiB, about 6% of the data, shared by both caches |
| Run length | 8192 block-sized operations |
| Repetitions | 3 seeds per configuration; tables show the median |
| I/O | image opened with `O_DIRECT` on ext4, so each cache miss is a real device read |
| Think time | 0 us (requests back to back) and 200 us (an application that works on each block) |
| Machine | WSL2 Linux 6.6, 20 logical CPUs, virtual disk |

Workloads:

- **sequential**: every file read front to back.
- **random**: uniformly random blocks.
- **repetitive**: one fixed random order of 600 blocks, repeated. It is longer than the cache holds, so LRU alone cannot keep it.
- **mixed**: sequential, repetitive and random phases of 512 operations each, with one operation in ten a write.

The benchmark calls the filesystem library directly rather than through a
mount. FUSE adds the same per-request overhead to every version, so leaving it
out makes the differences between policies easier to see; absolute latencies
through a mount will be higher.

## Read latency

![Average read latency](../experiments/read_avg_us.png)

Average read latency in microseconds (lower is better):

| Workload | Think 0: A | B | C | Think 200: A | B | C |
|---|---:|---:|---:|---:|---:|---:|
| sequential | 162 | 120 | 64 | 379 | 95 | 74 |
| random | 309 | 290 | 293 | 484 | 422 | 499 |
| repetitive | 281 | 191 | 139 | 498 | 155 | 155 |
| mixed | 255 | 262 | 197 | 395 | 323 | 252 |

- With no think time, B's two-block lookahead has little time to get ahead of the reader. C raises its depth to 8 and loads 32 mappings per query, which is where most of its sequential gain comes from.
- With 200 us of think time, prefetches finish before the next request, so B closes most of the gap.
- On the mixed workload with no think time B is slightly slower than A: its wrong guesses in the random phases cost image reads that compete with the reads that matter.
- Every version is slower with think time, A included. The reader thread sleeps between requests and pays a wake-up cost on each one; compare versions within a column, not across columns.

## Cache hit rate and prefetching

![Cache hit rate](../experiments/cache_hit_rate.png)
![Wasted prefetches](../experiments/prefetch_wasted.png)

With no think time (the think-time runs are within a point of these):

| Workload | Hit rate A | B | C | Prefetch precision B | C | Wasted B | C |
|---|---:|---:|---:|---:|---:|---:|---:|
| sequential | 0.000 | 0.997 | 0.997 | 1.000 | 1.000 | 0 | 0 |
| random | 0.056 | 0.054 | 0.053 | 0.043 | none issued | 2,048 | 0 |
| repetitive | 0.046 | 0.808 | 0.831 | 0.974 | 0.860 | 169 | 1,067 |
| mixed | 0.039 | 0.454 | 0.538 | 0.674 | 0.952 | 1,510 | 188 |

Precision is the share of prefetched blocks that were read before being
evicted. A's hit rate is near zero on purpose: the cache is far smaller than
the data and none of these workloads revisits a block soon enough for LRU.

- **Random:** B's follower table treats a block seen once before as a 50% guess and prefetches on it. C scores those guesses, sees they are wrong, and silences the table.
- **Mixed:** the same mechanism lifts C's precision from 0.67 to 0.95 while still serving more reads from prefetch than B.
- **Repetitive:** the 600-block order contains repeated blocks, so some blocks have two possible followers. At depth 8 a wrong turn costs up to 8 blocks; at B's depth 2 it costs 2. Overall precision stays above the 80% bar, so the feedback rule never backs off.

## Disk I/O

![Blocks read from the image](../experiments/disk_reads.png)

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,761 |
| repetitive | 7,815 | 7,988 | 8,956 |
| mixed | 7,100 | 9,058 | 7,736 |

Prefetching does not reduce the number of blocks read; it moves reads off the
request path, and wrong guesses add reads. B adds 28% on random and mixed. C
adds none on random, 9% on mixed and 15% on repetitive.

## Metadata indexing overhead

![Time in mapping queries](../experiments/index_time_ms.png)

Total time in block-mapping queries, milliseconds, no think time:

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 107 | 172 | 8 |
| random | 108 | 117 | 93 |
| repetitive | 9 | 9 | 13 |
| mixed | 84 | 116 | 39 |

- B is worse than A here: looking up the mapping of each predicted block one at a time adds queries.
- C's range query cuts sequential mapping queries from 8,225 to about 300.
- On random, C moves memory from block data to mappings (the data share ends between 0.75 and 0.85) and cuts queries from about 5,300 to between 4,300 and 5,200, depending on the run. The saving is small because the trade also costs a few block-cache hits.

## Memory and writes

- All versions stay inside the 1024 KiB budget (954 to 1,022 KiB in use at the end of each run).
- Write latency (mixed workload only) is 261 / 247 / 226 us for A / B / C with no think time. Writes are full-block overwrites and go straight to the image in every version, so these differences come from the surrounding reads, not from a different write path.

## Limits of this evaluation

- Three runs per configuration, medians only; no confidence intervals. Differences of 10% or less, including all of the random workload's latency numbers, should be read as no difference.
- One machine, one cache size, one file layout, synthetic workloads. No traces from real applications.
- Throughput is operations per second of time spent inside the filesystem, excluding think time.
- The predictor is a statistical model. The scikit-learn models named in the project plan were not built or compared.
- Versions A and B use a 90/10 memory split that was chosen, not tuned.
