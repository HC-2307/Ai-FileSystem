# Evaluation

All numbers here were measured on one machine by `python/evaluate.py`. The raw
rows are in [experiments/results.csv](../experiments/results.csv) and every
metric is tabulated in [experiments/summary.md](../experiments/summary.md).

Version C is the project. It uses two machine-learning models, both trained in
Python while the filesystem runs:

- a scikit-learn decision tree that predicts which blocks will be read next, which the filesystem follows to prefetch them;
- a scikit-learn linear regression that learns how to split the cache memory between file data and block mappings.

A (no prediction) and B (hand-written prediction) are the comparisons.

## Summary

- **The tree learns the patterns by itself.** It predicts sequential and repeating reads and predicts nothing on random ones.
- **No think time:** C has the lowest read latency on every workload: 87 us against B's 116 on sequential, 125 against 195 on repetitive, 212 against 275 on mixed.
- **With 200 us of think time:** C is ahead of B on sequential (76 against 81 us) and repetitive (139 against 178 us), and behind on mixed (351 against 333 us).
- **Waste:** C wastes no prefetches on the sequential, random and repetitive workloads, where B wastes about 2,050 on random and 169 on repetitive. On mixed C wastes about 420, against B's 1,510.
- **The memory split cuts database lookups on scattered reads** (11% fewer on random, 48% fewer on mixed than version A) but does not change read latency measurably.
- **Where C is behind:** on the mixed workload it serves 24% of reads from prefetch against B's 42%. And when neither cache is short of memory, the split model has nothing to learn from and leaves part of the budget unused.

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
| Models | started fresh for every run, so they have seen nothing before the first read |
| Tree settings | 4 reads ahead, trained on the last 2,048 reads, retrained every 256 reads |
| Split settings | reviewed every 128 reads, 5% steps, between 50% and 95% for file data, starting at 90% |

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
| sequential | 148 | 116 | 87 | 414 | 81 | 76 |
| random | 329 | 321 | 282 | 543 | 547 | 469 |
| repetitive | 347 | 195 | 125 | 524 | 178 | 139 |
| mixed | 252 | 275 | 212 | 497 | 333 | 351 |

- **No think time:** C is fastest everywhere. It predicts 4 blocks ahead where B predicts 2, so more prefetches finish before the block is asked for, and it loads block mappings 32 at a time.
- **With think time:** C stays ahead on sequential and repetitive reads. On mixed reads it is behind B, because it predicts fewer of them.
- **Random:** C measures faster than A and B, but nothing was prefetched, so the tree cannot be the cause. Fewer mapping queries explain a few microseconds at most. Timings on this machine also shift with what else is running (version A alone got about 25% faster while an unrelated program kept one processor busy). Read the random row as "no reliable difference".
- Every version is slower with think time, A included. The reader thread sleeps between requests and pays a wake-up cost on each one; compare versions within a column, not across columns.

## What the tree learned

Share of reads served from a prefetched block (the two think-time columns agree to within a point):

| Workload | B | C |
|---|---:|---:|
| sequential | 0.997 | 0.996 |
| random | 0.011 | 0.000 |
| repetitive | 0.768 | 0.825 |
| mixed | 0.421 | 0.236 |

- **Sequential:** the same as B.
- **Repetitive:** better than B. The order contains repeated blocks, so some blocks have two possible followers. The tree is told the two jumps before each read and can tell them apart; B's table cannot.
- **Mixed:** this is the tree's weak case. The pattern changes every 512 operations, and the tree only learns a new pattern at its next training (up to 256 reads later) and only after seeing it twice. Its window also holds the random phases, which it has to work around.
- **Random:** it predicts nothing, because no leaf of the tree is pure.

The tree's accuracy figure (running share of reads that were its first guess)
ends at 1.00 on sequential and repetitive reads, 0.35 on mixed and 0 on random.

## Cache hit rate and prefetching

![Cache hit rate](../experiments/cache_hit_rate.png)
![Wasted prefetches](../experiments/prefetch_wasted.png)

With no think time:

| Workload | Hit rate A | B | C | Prefetch precision B | C | Wasted B | C |
|---|---:|---:|---:|---:|---:|---:|---:|
| sequential | 0.000 | 0.997 | 0.996 | 1.000 | 1.000 | 0 | 0 |
| random | 0.056 | 0.054 | 0.042 | 0.043 | none issued | 2,047 | 0 |
| repetitive | 0.046 | 0.809 | 0.860 | 0.974 | 1.000 | 169 | 0 |
| mixed | 0.039 | 0.453 | 0.262 | 0.673 | 0.810 | 1,510 | 422 |

Precision is the share of prefetched blocks that were read before being
evicted. A's hit rate is near zero on purpose: the cache is far smaller than
the data and none of these workloads revisits a block soon enough for LRU.

B prefetches on any block it has seen once before. The tree only predicts from
a pure leaf, so on a steady pattern it is always right. On the mixed workload
it is wrong about a fifth of the time: a tree trained on one phase keeps
predicting into the next.

## The memory split

The split starts at 90% for file data. Where it ended, in each of the three
runs with no think time:

| Workload | Final share for file data | Mapping queries: A | C | Cache memory in use at the end (KiB) |
|---|---|---:|---:|---:|
| sequential | 95%, 95%, 50% | 8,192 | 287 | 1,023 |
| random | 50%, 50%, 50% | 5,313 | 4,725 | 708 |
| repetitive | 50%, 50%, 50% | 559 | 559 | 546 |
| mixed | 50%, 50%, 50% | 4,660 | 2,443 | 825 |

- **Random and mixed:** the mapping cache is the one that is short of memory, and the model learns to give it more. Mapping queries fall by 11% on random and 48% on mixed against version A (with think time: 29% and 55%). On mixed the fixed 32-mappings-per-query lookup also contributes; a version of C without the split made 3,854 queries there.
- **The price:** less memory for file data. On random reads the block cache's hit rate falls from 5.6% to 4.2% and disk reads rise from 7,735 to 7,845.
- **Net effect on read latency:** none that could be measured. Timed side by side over 12 runs each, C with and without the split differed by 1 to 2 us on sequential and repetitive reads, well inside the spread between runs.
- **Sequential:** the three runs did not agree, so nothing was learned there. Neither cache is short of memory in a sequential scan.
- **Repetitive:** all three runs ended at 50%, but this is not a good decision. The 600-block pattern needs few mappings, so the mapping cache cannot use what it is given, and about 400 KiB of the budget goes unused. It costs nothing here only because prefetching already covers the reads.

The model sees 64 reviews in one run, and the first 8 moves are random tries,
so these runs show it early in its learning. What each review found is stored
in SQLite and shown to the model again at the next mount; a filesystem in
longer use was not measured.

## Disk I/O

![Blocks read from the image](../experiments/disk_reads.png)

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 8,192 | 8,192 | 8,192 |
| random | 7,735 | 9,919 | 7,845 |
| repetitive | 7,815 | 7,988 | 7,908 |
| mixed | 7,100 | 9,062 | 7,816 |

Prefetching does not reduce the number of blocks read; it moves reads off the
request path, and wrong guesses add reads. B adds 28% on random and mixed. C
adds 1% on random and repetitive (from the smaller block cache) and 10% on
mixed.

## Metadata indexing overhead

![Time in mapping queries](../experiments/index_time_ms.png)

Total time in block-mapping queries, milliseconds, no think time:

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 105 | 165 | 9 |
| random | 128 | 138 | 77 |
| repetitive | 13 | 12 | 9 |
| mixed | 86 | 125 | 40 |

- B is worse than A on sequential and mixed: looking up the mapping of each predicted block one at a time adds queries.
- When C looks up a predicted block it loads the next 32 mappings in the same query. That cuts sequential mapping queries from 8,192 to about 290. This is not machine learning; it is a fixed part of how C looks up what the tree predicts, and it is part of why C's sequential latency is low.
- On random and mixed reads the saving comes from the learned memory split as well (see above).

## Memory and writes

- The caches stay inside the 1024 KiB budget in every version. C uses less than the budget on some workloads (546 to 1,023 KiB), for the reason given under "The memory split".
- The models are outside that budget: the 2,048 reads the tree model remembers, the Python process, and the copy of the tree the filesystem holds.
- Write latency (mixed workload only) is 263 / 262 / 200 us for A / B / C with no think time. Writes are full-block overwrites and go straight to the image in every version.

## Against the earlier rule-based version C

Version C was first built as version B plus hand-written feedback rules. Those
rules were removed and replaced by the two models. Their last measured results
(commit `63f1603`, same machine and settings, no think time):

| Workload | Read latency: rules | models | Reads served by prefetch: rules | models | Wasted prefetches: rules | models |
|---|---:|---:|---:|---:|---:|---:|
| sequential | 64 | 87 | 0.997 | 0.996 | 0 | 0 |
| random | 293 | 282 | 0 | 0 | 0 | 0 |
| repetitive | 139 | 125 | 0.792 | 0.825 | 1,067 | 0 |
| mixed | 197 | 212 | 0.508 | 0.236 | 188 | 422 |

- **Repetitive:** the models are faster, predict more and waste nothing.
- **Sequential:** the rules were faster. They prefetched up to 8 blocks ahead; the tree looks 4 ahead.
- **Mixed:** the rules were faster and predicted about twice as many reads. They reacted to a new pattern within a few reads; the tree waits for its next training.
- The two sets of numbers come from runs on different days, and timings on this machine drift by tens of microseconds between runs. The size of the gaps is approximate.

## Earlier designs of the tree model

Three designs were measured before this one. Each kept all prediction in
Python, with the filesystem sending a read and using the answer when it
arrived. Average read latency, no think time:

| Workload | Neural network in Python | Decision tree in Python | Decision tree, followed in C++ (now) |
|---|---:|---:|---:|
| sequential | 141 | 163 | 87 |
| repetitive | 192 | 202 | 125 |
| mixed | 240 | 247 | 212 |

A Python program takes about 100 us to answer one read, longer than a cached
read takes, so its answers always arrived late. Having Python train the tree
and the filesystem follow it removed that delay. (Waiting for Python's answer
on every read was tried first and was slower than no prediction at all.)

## Limits of this evaluation

- Three runs per configuration, medians only; no confidence intervals. Differences of 10% or less should be read as no difference.
- One machine, one cache size, one file layout, synthetic workloads. No traces from real applications.
- Timings depend on what else the machine is doing. One full run of this evaluation was thrown away because version C alone ran 15% to 20% slow in it; the cause was not found and it did not happen again.
- The models start untrained in every run. A mounted filesystem that has been used before starts from its recorded reads and reviews, which was not measured.
- The models' settings were chosen by trying a few values on these same four workloads. They were not tuned on separate data.
- Only one kind of model was used for each job in the final system. Others were compared in a replay simulation, not in the filesystem.
- Throughput is operations per second of time spent inside the filesystem, excluding think time.
