# Evaluation

All numbers here were measured on one machine by `python/evaluate.py`. The raw
rows are in [experiments/results.csv](../experiments/results.csv) and every
metric is tabulated in [experiments/summary.md](../experiments/summary.md).

Version C is the project: a scikit-learn decision tree, trained on the reads
while the filesystem runs, that predicts which blocks to prefetch. A (no
prediction) and B (hand-written prediction) are the comparisons.

## Summary

- **The model learns the patterns by itself.** It predicts sequential and repeating reads and predicts nothing on random ones.
- **It is nearly always right when it predicts a clear pattern.** It wastes no prefetches on the sequential, random and repetitive workloads, where B wastes about 2,050 on random and 169 on repetitive. On the mixed workload it wastes about 390, against B's 1,510.
- **Against A (no prediction):** C is faster on sequential and repetitive reads in both columns, and on mixed reads when there is think time. With no think time, mixed is the same within noise.
- **Against B (hand-written), no think time:** C is slower on sequential (163 against 131 us) and about equal on repetitive and mixed.
- **Against B, with 200 us of think time:** C is faster on repetitive (129 against 156 us) and mixed (288 against 333 us), and slightly slower on sequential (86 against 79 us).
- **Where C is behind:** it serves fewer reads from prefetch than B whenever reads come faster than the model can answer, and on the mixed workload in both columns (17% to 22% against B's 42%).
- **Against the earlier rule-based version C:** the model is slower with no think time (see the last section).

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
| Model | started fresh for every run, so it has seen nothing before the first read |
| Model settings | 4 reads ahead, trained on the last 2,048 reads, retrained every 256 reads |

Workloads:

- **sequential**: every file read front to back.
- **random**: uniformly random blocks.
- **repetitive**: one fixed random order of 600 blocks, repeated. It is longer than the cache holds, so LRU alone cannot keep it.
- **mixed**: sequential, repetitive and random phases of 512 operations each, with one operation in ten a write.

The benchmark calls the filesystem library directly rather than through a
mount. FUSE adds the same per-request overhead to every version, so leaving it
out makes the differences easier to see; absolute latencies through a mount
will be higher.

## Read latency

![Average read latency](../experiments/read_avg_us.png)

Average read latency in microseconds (lower is better):

| Workload | Think 0: A | B | C | Think 200: A | B | C |
|---|---:|---:|---:|---:|---:|---:|
| sequential | 176 | 131 | 163 | 408 | 79 | 86 |
| random | 342 | 293 | 275 | 510 | 501 | 359 |
| repetitive | 314 | 194 | 202 | 503 | 156 | 129 |
| mixed | 243 | 257 | 247 | 508 | 333 | 288 |

- **No think time:** the model takes about 100 us per read, so when reads come back to back its answers fall behind and fewer blocks are prefetched in time. That costs most on sequential reads, where B's detector is instant.
- **With think time:** the model keeps up. It beats B on repetitive and mixed reads and is within 7 us of it on sequential.
- **Random:** C measures faster than A in both columns, but nothing was prefetched, so the model's predictions cannot be the cause. On this machine a second busy process shortens disk-read times (measured separately: version A alone got about 25% faster while an unrelated program kept one processor busy). Read the random row as "no difference".
- Every version is slower with think time, A included. The reader thread sleeps between requests and pays a wake-up cost on each one; compare versions within a column, not across columns.

## What the model learned

Share of reads served from a prefetched block:

| Workload | Think 0: B | C | Think 200: B | C |
|---|---:|---:|---:|---:|
| sequential | 0.997 | 0.794 | 0.997 | 0.968 |
| random | 0.011 | 0.000 | 0.011 | 0.000 |
| repetitive | 0.766 | 0.697 | 0.771 | 0.786 |
| mixed | 0.421 | 0.167 | 0.424 | 0.217 |

- **Sequential and repetitive:** given time to answer, the model covers about as many reads as B (more on repetitive). Without it, it covers 70% to 80%.
- **Repetitive:** the order contains repeated blocks, so some blocks have two possible followers. The tree is told the two jumps before each read and can tell them apart; B's table cannot.
- **Mixed:** this is the model's weak case in both columns. The pattern changes every 512 operations, and the model only learns a new pattern at its next training (up to 256 reads later) and only after seeing it twice. Its window also holds the random phases, which the tree has to work around.
- **Random:** it predicts nothing, because no leaf of the tree is pure.

The model's own accuracy figure (share of recent reads that were its first
guess) ends at 0.99 on sequential and repetitive reads with think time, and
0.33 on mixed. With no think time it reads lower (0.76, 0.79, 0.28), because a
read the model had no time to predict counts as a miss.

## Cache hit rate and prefetching

![Cache hit rate](../experiments/cache_hit_rate.png)
![Wasted prefetches](../experiments/prefetch_wasted.png)

With no think time:

| Workload | Hit rate A | B | C | Prefetch precision B | C | Wasted B | C |
|---|---:|---:|---:|---:|---:|---:|---:|
| sequential | 0.000 | 0.997 | 0.794 | 1.000 | 1.000 | 0 | 0 |
| random | 0.056 | 0.054 | 0.056 | 0.043 | none issued | 2,048 | 0 |
| repetitive | 0.046 | 0.808 | 0.748 | 0.974 | 1.000 | 169 | 0 |
| mixed | 0.039 | 0.454 | 0.205 | 0.674 | 0.764 | 1,510 | 391 |

Precision is the share of prefetched blocks that were read before being
evicted. A's hit rate is near zero on purpose: the cache is far smaller than
the data and none of these workloads revisits a block soon enough for LRU.

B prefetches on any block it has seen once before. The model only predicts
from a pure leaf, so on a steady pattern it is always right. On the mixed
workload it is wrong about a quarter of the time: a tree trained on one phase
keeps predicting into the next.

## Disk I/O

![Blocks read from the image](../experiments/disk_reads.png)

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,735 |
| repetitive | 7,815 | 7,988 | 7,814 |
| mixed | 7,100 | 9,060 | 7,650 |

Prefetching does not reduce the number of blocks read; it moves reads off the
request path, and wrong guesses add reads. B adds 28% on random and mixed. C
adds none on random and 8% on mixed.

## Metadata indexing overhead

![Time in mapping queries](../experiments/index_time_ms.png)

Total time in block-mapping queries, milliseconds, no think time:

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 115 | 183 | 17 |
| random | 133 | 130 | 84 |
| repetitive | 13 | 12 | 9 |
| mixed | 80 | 115 | 69 |

- B is worse than A on sequential and mixed: looking up the mapping of each predicted block one at a time adds queries.
- When C looks up a predicted block it loads the next 32 mappings in the same query. That cuts sequential mapping queries from 8,192 to about 725. This is not machine learning; it is a fixed part of how C looks up what the model predicts.
- C's lower time on random is the same machine effect as in the latency table: it ran the same 5,313 queries as A.

## Memory and writes

- The caches stay inside the 1024 KiB budget in every version (954 to 1,022 KiB in use at the end of each run).
- The model is outside that budget: the 2,048 reads it remembers, the tree, and the Python process itself.
- Write latency (mixed workload only) is 250 / 242 / 231 us for A / B / C with no think time. Writes are full-block overwrites and go straight to the image in every version.

## Model against the earlier rule-based version C

Version C used to be version B plus hand-written feedback rules. Those rules
were removed and replaced by the model. Their last measured results (commit
`63f1603`, same machine and settings, no think time):

| Workload | Read latency: rules | model | Reads served by prefetch: rules | model | Wasted prefetches: rules | model |
|---|---:|---:|---:|---:|---:|---:|
| sequential | 64 | 163 | 0.997 | 0.794 | 0 | 0 |
| random | 293 | 275 | 0 | 0 | 0 | 0 |
| repetitive | 139 | 202 | 0.792 | 0.697 | 1,067 | 0 |
| mixed | 197 | 247 | 0.508 | 0.167 | 188 | 391 |

- The rule-based version was faster on sequential, repetitive and mixed reads. Its rules ran inside the filesystem with no delay and prefetched up to 8 blocks ahead; the model runs in another process, answers late, and looks 4 ahead.
- The model wastes fewer prefetches on the repetitive workload and more on the mixed one.
- The two sets of numbers come from runs on different days, and timings on this machine drift by tens of microseconds between runs (A's sequential latency was 162 then and 176 now). The size of the gaps is approximate; their direction on sequential, repetitive and mixed is not in doubt.

## Limits of this evaluation

- Three runs per configuration, medians only; no confidence intervals. Differences of 10% or less should be read as no difference.
- One machine, one cache size, one file layout, synthetic workloads. No traces from real applications.
- Timings depend on what else the machine is doing, including the model's own process (see the random workload above).
- The model starts untrained in every run. A mounted filesystem that has been used before starts from its recorded reads, which was not measured.
- The model's settings were chosen by trying a few values on these same four workloads. They were not tuned on separate data.
- Only one kind of model was tried in the final system. Other scikit-learn models were not compared.
- Throughput is operations per second of time spent inside the filesystem, excluding think time.
