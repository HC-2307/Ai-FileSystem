# Architecture

## Layers

```
ordinary programs (cp, cat, editors)
        |  kernel FUSE
src/main.cpp          FUSE adapter: one callback per operation, errno translation
        |
src/filesystem.cpp    files and directories, block allocation, the three policies
   |          |
   |     SQLite database      inodes, file_blocks, free_blocks, access_log
   |
src/block_cache.cpp   LRU cache of blocks, prefetch worker threads
        |
src/block_storage.cpp one image file of 4096-byte blocks (optionally O_DIRECT)
```

`src/predictor.cpp` and `include/policy.h` hold the prediction model and the
feedback rules. They have no I/O of their own; `filesystem` calls them.

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
written to `access_log` in batches of 4096 and on close. When a filesystem is
opened in version B or C, the last 50,000 recorded reads are replayed into the
predictor, so it starts with what earlier sessions learned.

## Two caches, one memory budget

| Cache | Holds | A miss costs |
|---|---|---|
| Block cache | contents of image blocks (4096 bytes each) | one read from the image |
| Mapping cache | which image block holds (file, index) (about 64 bytes each) | one SQLite query |

Both are LRU. They share one budget (`--cache-mb`). Versions A and B give 90%
to block data and 10% to mappings, fixed. Writes go through the block cache to
the image immediately, so the cache never holds data the image lacks.

## The three versions

| | A: baseline | B: prediction | C: adaptive |
|---|---|---|---|
| Caches | LRU, fixed 90/10 split | same as A | split is retuned while running |
| Prediction | none | stride + follower table | same, each model weighted by its measured accuracy |
| Prefetch depth | none | 2 blocks | 1 to 8, tuned by feedback |
| Confidence needed | n/a | 0.5 | 0.3 to 0.9, tuned by feedback |
| Mapping lookups for predicted blocks | n/a | one query per block | one query loads the next 32 |

## Prediction

Two models look at each block read:

- **Stride.** Per file, the difference between consecutive block indexes. When
  the same difference repeats `n` times in a row, the next blocks at that
  stride are predicted with confidence `n / (n + 1)`. This catches a
  sequential scan on its third read.
- **Follower table.** For every block, a count of which block was read next.
  The most frequent follower is predicted with confidence
  `count / (total + 1)`, and the chain is followed for deeper predictions,
  multiplying confidences. This catches any order that repeats, whatever it is.

A predicted block whose confidence clears the threshold is handed to four
background threads, which read it into the block cache. A block that is
already cached is marked recently used instead, so it is not evicted just
before it is needed.

## Feedback (version C)

Three loops, each driven by measured outcomes:

1. **Is each model right?** Every guess is checked against the read that
   actually follows. Each model keeps a running accuracy, and its confidence
   is multiplied by `min(1, 2 x accuracy)`. A model that is wrong most of the
   time falls below the threshold and stops causing prefetches, but keeps
   being scored, so it comes back if the workload changes.
2. **Were prefetches used?** The block cache counts prefetched blocks that
   were read (used) and those evicted unread (wasted). After every 16
   outcomes: 80% or more used raises depth and lowers the threshold; under
   50% used does the opposite.
3. **Which cache should get memory?** Each cache keeps a ghost list: the keys
   it would still hold if it had 5% more of the budget. A miss on a ghost key
   is a miss that the extra memory would have avoided. Every 256 block reads
   the policy multiplies each cache's ghost hits by the measured cost of one
   miss there (average image read time, average SQLite query time) and moves
   5% of the budget to the larger product. A mapping cache with unused room
   is never given more.

Loop 3 is where the cache and the metadata index are tuned together, and it is
resource-aware in that it weighs memory against measured time.

## Correctness does not depend on prediction

- Prefetching only fills the cache; every read still resolves its own mapping and checks the cache or the image.
- A prefetch that is under way when its block is written or freed is discarded, so it cannot bring back old contents.
- The same tests run against all three versions, including a randomized test that compares each against an in-memory model with a cache far smaller than the data.

## Known limits

- Single-threaded: the mount runs with `-s`. Only prefetch reads run in parallel.
- No permissions, ownership, timestamps, symbolic or hard links.
- `access_log` grows without bound.
- The follower table is dropped whole when it reaches 262,144 blocks.
- Deleting or truncating a file empties the whole mapping cache.
