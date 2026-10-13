# Architecture

## Layers

```
ordinary programs (cp, cat, editors)
        |  kernel FUSE
src/main.cpp          FUSE adapter: one callback per operation, errno translation
        |
src/filesystem.cpp    files and directories, block allocation, the three versions
   |          |    \
   |          |     src/model_client.cpp  <--pipes-->  python/model.py   (version C)
   |     SQLite database      inodes, file_blocks, free_blocks, access_log
   |
src/block_cache.cpp   LRU cache of blocks, prefetch worker threads
        |
src/block_storage.cpp one image file of 4096-byte blocks (optionally O_DIRECT)
```

`src/predictor.cpp` is version B's hand-written predictor. Version C does not
use it.

## Storage and metadata

- File data lives in one preallocated image file, addressed by block number.
- SQLite holds everything else:
  - `inodes`: one row per file or directory (parent, name, size).
  - `file_blocks`: which image block holds block `idx` of a file. A missing row is a hole and reads as zeros.
  - `free_blocks`: unallocated blocks. Allocation takes the lowest.
  - `access_log`: every block read and write, in order.
- A write runs in one SQLite transaction. If it fails (for example the image is full), the metadata rolls back.
- No crash consistency is claimed: data blocks are not journaled.

## Access monitoring

Every block-level read and write is appended to an in-memory buffer and
written to `access_log` in batches of 4096 and on close. This log is the
model's training history: when a filesystem is opened in version C, the last
8,192 recorded reads are sent to the model before any new read is served (it
keeps the newest 2,048).
Version B replays the last 50,000 into its detectors in the same way.

## Two caches

| Cache | Holds | A miss costs |
|---|---|---|
| Block cache | contents of image blocks (4096 bytes each) | one read from the image |
| Mapping cache | which image block holds (file, index) (about 64 bytes each) | one SQLite query |

Both are LRU and share one budget (`--cache-mb`): 90% for block data and 10%
for mappings, in every version. Writes go through the block cache to the image
immediately, so the cache never holds data the image lacks.

## The three versions

Version C is the project. A and B exist to compare it against.

| | A: baseline | B: hand-written prediction | C: learned model |
|---|---|---|---|
| Caches | LRU | same | same |
| What predicts | nothing | stride detector + follower table, in C++ | a scikit-learn decision tree, trained in Python |
| How it decides | n/a | fixed: 2 blocks ahead, confidence 0.5 | predicts 4 blocks ahead when the tree's leaf is pure |
| Learns | no | counts what it has seen | retrained on the last 2,048 reads every 256 reads |
| Mapping lookups for predicted blocks | n/a | one query per block | one query loads the next 32 |

## The model (version C)

The model is `python/model.py`, about 140 lines of Python. It uses
`DecisionTreeRegressor` from scikit-learn.

**What it predicts.** A *jump* is how far the next read is from the current
one: how many files along and how many blocks along. Reading a file front to
back is a jump of +1 block every time. The model predicts jumps, not blocks,
so what it learns on one file works on every file.

**Inputs and outputs.** For each read the tree is given six numbers: the file,
the block, and the two jumps that led to this read. It predicts eight numbers:
the next four jumps. Adding them to the current block gives four blocks to
prefetch.

**Training.** The model keeps the last 2,048 reads. Every one of them is a
training example: "after this read, these four jumps followed". A new tree is
fitted to those examples after 16, 32, 64 and 128 reads, and then every 256
reads. Because old reads fall out of the window, the tree follows the workload
as it changes. This is the feedback loop: what really happened becomes the
training data for the next tree.

**Knowing when not to predict.** Each leaf of the tree holds the training
examples that ended up there, at least two. If they all continued the same way
the leaf is pure, and the model predicts. If they disagree, the reads had no
pattern the tree could find, and the model predicts nothing. On random reads
almost every leaf is impure.

**Python learns, the filesystem predicts.** Asking a Python program about every
single read is too slow: it takes about 100 microseconds, longer than a cached
read. So the work is split:

1. Version C starts the Python program when the filesystem is opened and keeps two pipes to it.
2. For every block read, the filesystem writes one line to it (`R <file> <block>`) and carries on without waiting.
3. Each time Python has trained a new tree, it writes the tree back as plain numbers, one line per node.
4. The filesystem keeps the newest tree and follows it itself for every read (`src/model_client.cpp`).

A tree written out looks like this:

```
T 5                      a tree of 5 nodes follows
3 0.5 1 2                node 0: is input 3 <= 0.5? yes: go to node 1, no: go to node 2
-1 1 0 1 0 1 0 1 0 1     node 1: a leaf, pure; the next four jumps are +1 block each
1 200.5 3 4              node 2: another question
-1 0                     node 3: a leaf that is not pure: predict nothing
-1 1 0 7 0 -3 0 7 0 -3   node 4: a leaf, pure, with its four jumps
```

Following it means starting at node 0 and answering questions until a leaf is
reached, which takes well under a microsecond. The C++ side has no learning in
it and no rules of its own: every decision it makes is read from the tree that
scikit-learn trained. The whole protocol is described at the top of
`python/model.py`.

## Version B's predictor

Two hand-written detectors in `src/predictor.cpp`, kept as the comparison:

- **Stride.** Per file, the difference between consecutive block indexes. When
  the same difference repeats `n` times in a row, the next 2 blocks at that
  stride are predicted with confidence `n / (n + 1)`.
- **Follower table.** For every block, a count of which block was read next.
  The most frequent follower is predicted with confidence
  `count / (total + 1)`, and the chain is followed one step further.

A prediction with confidence 0.5 or more is prefetched.

## Prefetching

A predicted block is handed to four background threads, which read it into
the block cache. A block that is already cached is skipped.

## Correctness does not depend on prediction

- Prefetching only fills the cache; every read still resolves its own mapping and checks the cache or the image.
- A prefetch that is under way when its block is written or freed is discarded, so it cannot bring back old contents.
- A predicted block that does not exist (past the end of a file, or in a deleted file) is ignored.
- A tree that does not make sense (a line that points outside the tree, for example) is not followed.
- If the model cannot be started, opening the filesystem in version C fails with an error. If it goes away later, the filesystem carries on with the last tree it sent.
- The same tests run against all three versions, including a randomized test that compares each against an in-memory model with a cache far smaller than the data.

## Known limits

- Single-threaded: the mount runs with `-s`. Only prefetch reads run in parallel.
- No permissions, ownership, timestamps, symbolic or hard links.
- `access_log` grows without bound.
- The model's settings (4 reads ahead, a window of 2,048 reads, retraining every 256, leaves of at least 2) are fixed numbers chosen by hand.
- The model's memory (2,048 remembered reads, the tree, and the Python process) is outside the cache budget, as B's follower table is.
- The tree is not saved. Each mount starts a new model and trains it on the recorded reads.
- The model learns a new pattern only at its next training, up to 256 reads later, and only once the pattern has shown up twice in its window.
- The cache split is fixed at 90/10 in every version; nothing tunes it.
- B's follower table is dropped whole when it reaches 262,144 blocks.
- Deleting or truncating a file empties the whole mapping cache.
