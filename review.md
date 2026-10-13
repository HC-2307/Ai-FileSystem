# Final review plan

This is the plan for presenting the project. It assumes about 15 minutes of
talking and demo, then questions. If you get less time, drop step 5 of the
talk and the second half of the demo.

## The project in one sentence

A Linux filesystem that keeps its data in blocks and its metadata in SQLite,
and uses a machine-learning model (a scikit-learn decision tree, trained on
the reads while the filesystem runs) to predict which blocks will be read next
and load them ahead of time.

Say this first, and say it again at the end.

## Fix before the review

Two things are not ready yet. Do these first.

1. **`project.md` promises more than version C now does.** It lists adaptive
   cache management, adaptive metadata indexing, cache/index co-optimization
   and resource-aware decisions for version C. Those were hand-written rules
   and were removed when version C became machine learning only. Version C
   today is: prediction and prefetching by the model, and nothing else
   adaptive. Decide which you want:
   - update `project.md` so its version C section and research question match what was built, or
   - leave it and say so yourself in the review (see "Say these yourself" below).

   Do not let the professor find the gap first.
2. **One cleanup commit is still pending.** It is entry 2 in `commitMem.md`
   (the `.gitignore` line and the stray `experiments/bench_130177.db-journal`
   file). Commit it so `git status` is clean if anyone looks.

## The day before

Run all of this on the machine you will present from.

1. Build from scratch and run the tests:

   ```sh
   rm -rf build
   cmake -S . -B build && cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   Expected: `100% tests passed, 0 tests failed out of 5`. It takes about a minute.
2. Run the whole demo below once, start to finish, and time yourself.
3. Open these in tabs, in this order: `docs/architecture.md`,
   `python/model.py`, `src/model_client.cpp`, `experiments/read_avg_us.png`,
   `experiments/prefetch_wasted.png`, `docs/evaluation.md`.
4. Read "Questions to expect" below out loud once.
5. Take screenshots of the demo output and of the two charts, in case the demo
   fails on the day.

## Ten minutes before

1. Check nothing is left mounted: `mountpoint build/demo/mnt` should say "is not a mountpoint" or "No such file".
2. Open one terminal in the project folder with a large font.
3. Close anything heavy. Timings on this machine change with what else is running.
4. Run `ctest --test-dir build` once more.

## The talk

| # | Minutes | What to show | What to say |
|---|---:|---|---|
| 1 | 1 | nothing | The problem: a filesystem usually reacts to reads. If it could guess the next read, the data would already be in memory. The one-sentence description above. |
| 2 | 2 | the layer diagram at the top of `docs/architecture.md` | How it is built: FUSE on top, the filesystem in C++, SQLite for metadata, one image file for data, an LRU cache in between. |
| 3 | 1 | the "three versions" table in `docs/architecture.md` | Version C is the project. A (no prediction) and B (hand-written prediction) exist only so there is something to compare against. |
| 4 | 3 | `python/model.py` | The model. Explain a "jump". The tree gets the file, the block and the last two jumps, and predicts the next four jumps. It is retrained every 256 reads on the last 2,048 reads. It only predicts from a pure leaf, which is how it stays quiet on random reads. |
| 5 | 2 | the tree example in `docs/architecture.md`, then `predict()` in `src/model_client.cpp` | Why Python trains and C++ predicts: asking Python about every read takes about 100 microseconds, longer than a cached read. So Python sends the trained tree as lines of numbers and the filesystem follows it. Mention that you tried it the slow way first and measured it. |
| 6 | 3 | the terminal | The demo, below. |
| 7 | 2 | `experiments/read_avg_us.png`, then `experiments/prefetch_wasted.png` | The results, below. |
| 8 | 1 | nothing | What it does not do well, in your own words, then the one sentence again. |

## The demo

Run from the project folder. Each step says what you should see.

1. **Make a filesystem and mount it** (version C is the default):

   ```sh
   mkdir -p build/demo/mnt
   build/ai_fs format build/demo/disk.img build/demo/disk.db 25600
   build/ai_fs mount build/demo/disk.img build/demo/disk.db build/demo/mnt -f &
   ```

   Nothing is printed. `mountpoint build/demo/mnt` says "is a mountpoint".
2. **Use it like any folder:**

   ```sh
   head -c 8000000 /dev/urandom > build/demo/big.bin
   cp build/demo/big.bin build/demo/mnt/
   echo hello > build/demo/mnt/note.txt
   ls -l build/demo/mnt
   cat build/demo/mnt/note.txt
   cmp build/demo/big.bin build/demo/mnt/big.bin && echo identical
   ```

   You see both files, `hello`, and `identical`. Say: ordinary programs work on it, and what comes back is byte-for-byte what went in.

   The dates show 1 January 1970. That is expected: timestamps are not stored. Say so before anyone asks.
3. **Read the big file and unmount, to show the model working:**

   ```sh
   cat build/demo/mnt/big.bin > /dev/null
   fusermount3 -u build/demo/mnt
   ```

   On unmount it prints one line of statistics. In rehearsal it was:

   ```
   ai_fs: block reads 3909, cache hits 3849, disk reads 3908, disk writes 1955,
   prefetched 3848 (used 3848, wasted 0), mapping queries 1956
   ```

   Point at "prefetched 3848 (used 3848, wasted 0)": the model predicted nearly every block before it was asked for, and none of its predictions were wrong. Your numbers will differ slightly.
4. **Compare the three versions on the same workload:**

   ```sh
   cd build
   (./ai_fs_bench header; for v in A B C; do ./ai_fs_bench $v sequential; done) \
     | cut -d, -f1,2,8,12,15,17 | column -s, -t
   cd ..
   ```

   In rehearsal:

   ```
   version  workload    read_avg_us  cache_hit_rate  prefetch_wasted  prediction_coverage
   A        sequential  171.13       0.0000          0                0.0000
   B        sequential  145.78       0.9967          0                0.9967
   C        sequential  90.04        0.9962          0                0.9962
   ```

   Say: same data, same cache size, only the prediction differs. This takes about ten seconds.
5. **If there is time, the random workload:** the same command with `random` in place of `sequential`. Point at `prefetch_wasted`: B wastes about 2,000, C wastes 0.
6. **Clean up:** `rm -rf build/demo`

## The results to quote

Average read latency in microseconds, median of 3 runs, no think time between
reads. All of this is in `docs/evaluation.md`.

| Workload | A: no prediction | B: hand-written | C: the model |
|---|---:|---:|---:|
| sequential | 158 | 121 | 81 |
| random | 315 | 300 | 254 |
| repetitive | 303 | 193 | 121 |
| mixed | 242 | 265 | 212 |

Know these four by heart:

- **Sequential:** C is about twice as fast as no prediction (81 against 158).
- **Repetitive:** C serves 81% of reads from prefetch, B serves 77%, and C wastes none.
- **Random:** C makes no predictions at all; B wastes about 2,050 prefetches per run.
- **Mixed:** C serves only 24% of reads from prefetch; B serves 42%. This is the weak case.

## Say these yourself

Bring these up before you are asked. It reads as understanding, not weakness.

- **Random row:** C looks faster than A on random reads, but it prefetched nothing there, so the model is not the reason. Timings on this machine move with what else is running. Treat that row as "no difference".
- **Mixed workload:** when the pattern keeps changing, the model is behind B. It only learns a new pattern at its next training, and only after seeing it twice.
- **With 200 us of think time between reads:** C is level with B on sequential and behind B on mixed. C's clear wins are with reads back to back.
- **Not everything in C is machine learning:** when C looks up a predicted block it loads 32 block mappings in one database query. That is fixed code, and it is part of why C's sequential number is low.
- **The scope changed:** the first version C used hand-written feedback rules to tune prefetching and split cache memory. It was replaced so that version C would be machine learning only. The rules were faster on sequential and mixed reads (64 and 197 us against 81 and 212); the model is faster on repetitive (121 against 139) and wastes less.
- **The evaluation is small:** 3 runs per case, one machine, synthetic workloads, and the model's settings were picked on these same workloads.

## Questions to expect

**Why a decision tree and not a neural network?**
Both were built and measured. A small neural network (in NumPy) predicted the mixed workload better (about 40% of reads against the tree's 20% in a replay test). The tree is better on repeating patterns, trains in about 8 milliseconds, and can be written out as a short list of questions that the filesystem follows directly. scikit-learn's own neural network took far too long to train on a window of reads to be usable here.

**How is this "AI" and not just a rule?**
Nobody wrote "if the last jumps were +1, predict +1". The tree works that out from the recorded reads, and works out the 600-block repeating order the same way. Change the workload and the next tree is different. The C++ side only follows whatever tree it is given.

**Is it overfitting? It memorises the training reads.**
For this job memorising is partly the point: a repeating order should be remembered. The guard against memorising noise is that a leaf must hold at least two examples and they must agree. On random reads they do not agree, so nothing is predicted. The measured result is 0 wasted prefetches on random reads.

**Is the test data separate from the training data?**
Yes, by construction. The model is always trained on reads that have already happened and is judged on the reads that come after them. But the settings (window of 2,048, retrain every 256, 4 reads ahead) were chosen by trying values on these same four workloads.

**What happens if the model is wrong?**
Only speed is affected. Every read still looks up its own block and reads the cache or the disk. A wrong prediction loads a block nobody uses. The same tests run on all three versions, including one that compares every byte against an in-memory copy.

**What happens if the Python program crashes?**
The filesystem keeps working with the last tree it received. If Python cannot be started at all, mounting version C fails with an error message. Both cases have tests.

**Why SQLite for metadata?**
It gives transactions for free: a write that fails rolls back the metadata. It also stores the access log the model learns from, so a remounted filesystem starts from its history.

**Why is the cache so small in the benchmark?**
1 MiB against 16 MiB of data, about 6%. If the cache could hold everything, prefetching would have nothing to prove.

**Does the model's memory count against the cache budget?**
No. The 2,048 remembered reads, the tree and the Python process are outside it. Version B's follower table is outside it too.

**Does the benchmark go through the mounted filesystem?**
No. It calls the filesystem library directly, so FUSE's fixed cost per request is left out and the differences are easier to see. The mount is tested separately, and the demo uses it.

**What would you do next?**
Make the model react faster to a changing workload (the mixed case), test on traces from real programs, and add back adaptive cache management, this time learned.

## If something goes wrong

- **The mount does not appear:** show the screenshots, and run step 4 of the demo, which needs no mount.
- **Version C will not start ("could not start"):** the virtual environment is missing scikit-learn. Run `.venv/bin/pip install scikit-learn`, or mount with `--version B` to show the filesystem itself and use the screenshots for the model.
- **A folder is stuck mounted:** `fusermount3 -u build/demo/mnt`.
- **A number on screen differs from this page:** that is normal; timings vary from run to run. The order of A, B and C on sequential and repetitive reads is what holds.
- **You do not know an answer:** say what you measured and what you did not. "I did not test that" is a good answer in a final review; a guess is not.
