# Final review (Review II) plan

How the three of us present the project to the professor. The slides are in
`docs/final_presentation.pptx` (20 slides, with speaker notes on each).

Team: Harshit Choudhary (24BIT0237), Shravya Jain (24BIT0231), Arnav Aditya (24BIT0213).

## The project in one sentence

A Linux filesystem that keeps its data in blocks and its metadata in SQLite,
and uses machine learning (scikit-learn, trained on the reads while the
filesystem runs) to predict which blocks will be read next and to share out
its cache memory.

Say this first, and say it again at the end.

## What Review II asks for, and where we show it

| The guideline asks for | Where it is shown |
|---|---|
| Operating system concepts used | Slide 4, and questions in the viva |
| Complete working implementation | Demo steps 1 to 3 |
| Testing | Demo step 6 (`ctest`), slide 11 |
| Performance evaluation | Demo step 5, slides 13 to 16 |
| Error handling | Demo step 4, slide 11 |
| Demonstration of all features | Demo steps 1 to 5 |
| Documentation | `README.md`, `docs/architecture.md`, `docs/evaluation.md`, the final report |
| Viva voce | "Questions to expect" below |

## Who presents what

This is a suggested split so that every member speaks and every member can be
asked about a part. Change it to match who actually built what.

| Part | Slides | Speaker |
|---|---|---|
| Problem, objectives, OS concepts, architecture, storage | 1 to 7 | first member |
| The two models, testing, the demo | 8 to 12 | second member |
| Evaluation, limits, conclusion, future work | 13 to 20 | third member |

Everyone should be able to answer the questions for every part. The faculty
may ask any member anything.

## The day before

Do all of this on the laptop you will present from.

1. Build from scratch and run the tests:

   ```sh
   rm -rf build
   cmake -S . -B build && cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   Expected: `100% tests passed, 0 tests failed out of 5`. It takes one to two minutes.
2. Open `docs/final_presentation.pptx` in PowerPoint and click through all 20 slides. It was checked in LibreOffice, not in PowerPoint.
3. Run the whole demo below once, start to finish, and time it.
4. Take screenshots of each demo step and of the two charts, in case the demo fails on the day.
5. Each member reads "Questions to expect" out loud once.

## Ten minutes before

1. Nothing left mounted: `mountpoint build/demo/mnt` should say "is not a mountpoint" or "No such file".
2. One terminal open in the project folder, large font.
3. Close anything heavy. Timings on this machine change with what else is running.
4. Tabs open: the slides, `python/model.py`, `src/model_client.cpp`, `docs/evaluation.md`.

## Order of the session

About 15 minutes, then questions.

| # | Minutes | What |
|---|---:|---|
| 1 | 5 | Slides 1 to 11: problem, objectives, OS concepts, architecture, the three versions, the two models, testing |
| 2 | 5 | Slide 12, then the live demo |
| 3 | 4 | Slides 13 to 17: setup, results, what it does not do well |
| 4 | 1 | Slides 18 to 20: conclusion, future work, questions |

## The demo

Run from the project folder. Each step says what you should see. The outputs
shown are from a rehearsal on this machine; yours will differ slightly.

**1. Make a filesystem and mount it** (version C is the default):

```sh
mkdir -p build/demo/mnt
build/ai_fs format build/demo/disk.img build/demo/disk.db 25600
build/ai_fs mount build/demo/disk.img build/demo/disk.db build/demo/mnt -f &
```

Nothing is printed. `mountpoint build/demo/mnt` says "is a mountpoint".

**2. Use it like any folder:**

```sh
head -c 8000000 /dev/urandom > build/demo/big.bin
cp build/demo/big.bin build/demo/mnt/
echo hello > build/demo/mnt/note.txt
mkdir build/demo/mnt/docs
ls -l build/demo/mnt
cmp build/demo/big.bin build/demo/mnt/big.bin && echo identical
```

You see `big.bin`, `docs` and `note.txt`, then `identical`. Say: ordinary
programs work on it, and what comes back is byte-for-byte what went in.

The dates show 1 January 1970. That is expected: timestamps are not stored.
Say so before anyone asks.

**3. Read the big file and unmount, to show the models working:**

```sh
cat build/demo/mnt/big.bin > /dev/null
```

Do step 4 now, while it is still mounted, then come back and unmount:

```sh
fusermount3 -u build/demo/mnt
```

On unmount it prints one line of statistics. In rehearsal:

```
ai_fs: block reads 3908, cache hits 3807, disk reads 3908, disk writes 1955,
prefetched 3812 (used 3807, wasted 0), mapping queries 1956
```

Point at "prefetched 3812 (used 3807, wasted 0)": the tree predicted nearly
every block before it was asked for, and none of its predictions was wrong.

Then show what was recorded for the models to learn from:

```sh
sqlite3 build/demo/disk.db ".tables" \
  "select count(*) from access_log" \
  "select round(share,2), round(read_time/1000) from policy_log limit 4"
```

In rehearsal: the five tables, 5,863 logged accesses, and rows such as
`0.9|20.0`, `0.85|34.0`, `0.95|33.0`. Say: the first number is the share of
memory the block cache had, the second is the average read time in
microseconds that followed. This is the feedback the second model learns from.

**4. Error handling** (while the filesystem from step 1 is still mounted):

```sh
cat build/demo/mnt/missing.txt
rmdir build/demo/mnt/note.txt
mkdir build/demo/mnt/docs
```

Expected: `No such file or directory`, `Not a directory`, `File exists`.

A full disk, on a second, tiny filesystem of 1 MiB:

```sh
mkdir -p build/demo/small
build/ai_fs format build/demo/s.img build/demo/s.db 256
build/ai_fs mount build/demo/s.img build/demo/s.db build/demo/small -f &
cp build/demo/big.bin build/demo/small/
fusermount3 -u build/demo/small
```

Expected: `cp: error writing ... No space left on device`. Say: the write
that did not fit was rolled back in the database, so the metadata still
matches the blocks.

**5. Compare the three versions on the same workload:**

```sh
cd build
(./ai_fs_bench header; for v in A B C; do ./ai_fs_bench $v sequential; done) \
  | cut -d, -f1,2,8,12,15,17 | column -s, -t
cd ..
```

You see three rows with `read_avg_us` falling from A to B to C (about 150,
120 and 85 in rehearsal). Say: same data, same cache size, only the policy
differs. It takes about ten seconds.

If there is time, run it again with `random` in place of `sequential` and
point at `prefetch_wasted`: B wastes about 2,000, C wastes 0.

**6. The tests** (if asked, or if there is time; one to two minutes):

```sh
ctest --test-dir build
```

Expected: `100% tests passed, 0 tests failed out of 5`.

**7. Clean up:** `rm -rf build/demo`

## The results to quote

Average read latency in microseconds, median of 3 runs, no think time between
reads. All of this is in `docs/evaluation.md`.

| Workload | A: no prediction | B: hand-written | C: machine learning |
|---|---:|---:|---:|
| sequential | 148 | 116 | 87 |
| random | 329 | 321 | 282 |
| repetitive | 347 | 195 | 125 |
| mixed | 252 | 275 | 212 |

Know these by heart:

- **Sequential:** C takes 87 us against 148 with no prediction.
- **Repetitive:** C serves 82% of reads from prefetch, B serves 77%, and C wastes none.
- **Random:** C makes no predictions at all; B wastes about 2,050 prefetches per run.
- **Mixed:** C serves only 24% of reads from prefetch; B serves 42%. This is the weak case.
- **Memory split:** mapping queries fall from 5,313 to 4,725 on random and from 4,660 to 2,443 on mixed, with no measurable change in read latency.

## Say these yourself

Bring these up before you are asked. Slide 17 lists them.

- **Random row:** C looks faster than A on random reads, but it prefetched nothing there. Timings on this machine move with what else is running. Treat that row as "no reliable difference".
- **Mixed workload:** when the pattern keeps changing, the tree is behind B. It only learns a new pattern at its next training, and only after seeing it twice.
- **With 200 us of think time:** B is ahead of C on the mixed workload (333 against 351 us).
- **The memory split:** it cuts database lookups but does not make reads faster. When neither cache is short of memory it has nothing to learn from; on repetitive reads it drifted to 50% and left about 400 KiB of the budget unused.
- **Not everything in C is learned:** loading 32 block mappings per query is fixed code, and part of why C's sequential number is low.
- **How C got here:** the first version C used hand-written feedback rules. They were replaced by the two models so that C would be machine learning only. The rules were faster on sequential and mixed reads (64 and 197 us against 87 and 212); the models are faster on repetitive (125 against 139).
- **The evaluation is small:** 3 runs per case, one machine, synthetic workloads, and the settings were picked on these same workloads.

## Questions to expect

**Which operating system concepts does this use?**
File systems (blocks, inodes, directories, allocation); Linux system programming (FUSE, `pread`/`pwrite`, `O_DIRECT`, `fork`/`exec`, `poll`); inter-process communication (two pipes between the filesystem and Python); threads and synchronization (four prefetch threads, a mutex and condition variables around the cache); memory management (an LRU cache in a fixed budget, the same idea as page replacement); performance analysis.

**How does a read travel through the system?**
The program calls `read`. The kernel passes it to FUSE, which calls our `onRead`. The filesystem looks up the file, and for each block finds which image block holds it (mapping cache, or a SQLite query), then gets the block from the block cache or the image. It logs the access, asks the tree what comes next, and hands those blocks to the prefetch threads.

**Why LRU? How is it like page replacement?**
The cache holds a fixed number of blocks. When it is full, the block used longest ago is dropped, exactly as LRU page replacement drops a page. Prefetched blocks are put in early, and a predicted block that is already cached is marked recently used so it is not dropped just before it is needed.

**Where is synchronization needed?**
The block cache is shared by the filesystem thread and four prefetch threads. One mutex protects it. A condition variable wakes the prefetch threads when there is work, and another wakes a reader that is waiting for a block a prefetch thread is still loading.

**Why a decision tree and not a neural network?**
Both were built and measured. A small neural network predicted the mixed workload better. The tree is better on repeating patterns, trains in milliseconds, and can be written out as a short list of questions that the filesystem follows itself. scikit-learn's own neural network took far too long to train on a window of reads to be usable here.

**How is this learning and not just a rule?**
Nobody wrote "if the last jumps were +1, predict +1". The tree works that out from the recorded reads, and works out the 600-block repeating order the same way. Change the workload and the next tree is different. The C++ side only follows whatever tree it is given.

**Is it overfitting? It memorises the training reads.**
For this job memorising is partly the point: a repeating order should be remembered. The guard against memorising noise is that a leaf must hold at least two examples and they must agree. On random reads they do not agree, so nothing is predicted. The measured result is 0 wasted prefetches on random reads.

**Is the test data separate from the training data?**
Yes, by construction. The tree is always trained on reads that have already happened and judged on the reads that come after. But the settings (window of 2,048, retrain every 256, 4 reads ahead) were chosen by trying values on these same four workloads.

**How does the memory split learn?**
Every 128 reads the filesystem reports what a little more memory would have saved each cache, and how long reads took. A linear regression learns, from past moves, which move made reads faster. One time in ten it tries a random move so that it keeps learning. The reports are stored in SQLite.

**What happens if a prediction is wrong?**
Only speed is affected. Every read still looks up its own block and reads the cache or the disk. A wrong prediction loads a block nobody uses.

**What happens if the Python program crashes?**
The filesystem keeps working with the last tree and the last split it received. If Python cannot be started at all, mounting version C fails with an error message. Both cases have tests.

**Why SQLite for metadata?**
It gives transactions: a write that fails rolls back the metadata. It also stores the access log and the policy log the models learn from, so a remounted filesystem starts from its history.

**Why is the cache so small in the benchmark?**
1 MiB against 16 MiB of data, about 6%. If the cache could hold everything, prefetching would have nothing to prove.

**Does the benchmark go through the mounted filesystem?**
No. It calls the filesystem library directly, so FUSE's fixed cost per request is left out and the differences are easier to see. The mount is tested separately, and the demo uses it.

**Is it crash-safe?**
No. Metadata changes are transactions, but data blocks are not journaled. This is stated as a limit.

## If something goes wrong

- **The mount does not appear:** show the screenshots, and run demo step 5, which needs no mount.
- **Version C will not start ("could not start"):** the virtual environment is missing scikit-learn. Run `.venv/bin/pip install scikit-learn`, or mount with `--version B` to show the filesystem itself and use the screenshots for the models.
- **A folder is stuck mounted:** `fusermount3 -u build/demo/mnt`.
- **A number on screen differs from the slides:** that is normal; timings vary from run to run. The order of A, B and C on sequential and repetitive reads is what holds.
- **You do not know an answer:** say what you measured and what you did not. "We did not test that" is a good answer in a final review; a guess is not.
