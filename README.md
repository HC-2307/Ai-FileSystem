# AI-Enhanced Database-Backed Filesystem with Adaptive Storage Optimization

Operating Systems Lab (BITE303P) project, Fall 2026-27.

| Team member | Register number |
|---|---|
| Harshit Choudhary | 24BIT0237 |
| Shravya Jain | 24BIT0231 |
| Arnav Aditya | 24BIT0213 |

## What it is

A Linux user-space filesystem (FUSE) that keeps file data in fixed-size blocks
and metadata in SQLite, and uses machine learning to decide what to load ahead
of time and how to share its cache memory.

- **Predicting reads.** A scikit-learn decision tree is trained on the reads while the filesystem runs. Python trains the tree; the filesystem follows it to prefetch the next four blocks.
- **Sharing memory.** A second model (scikit-learn linear regression) learns from measured read times how to split the cache between file data and block mappings.

Three versions are built so the learned one can be compared fairly:

| Version | What it does |
|---|---|
| A | LRU caches, no prediction |
| B | hand-written prediction (a stride detector and a follower table) |
| C | the two machine-learning models; this is the project and the default |

## Documents

| File | What is in it |
|---|---|
| [project.md](project.md) | the proposal |
| [docs/architecture.md](docs/architecture.md) | how the system works |
| [docs/evaluation.md](docs/evaluation.md) | measured results |
| [docs/user_manual.md](docs/user_manual.md) | every command, option and error message |
| [docs/final_presentation.pptx](docs/final_presentation.pptx) | slides for the final review |
| [docs/project_proposal_report.docx](docs/project_proposal_report.docx) | project proposal report |
| [docs/review1_design_progress_report.docx](docs/review1_design_progress_report.docx) | design and progress report (Review I) |
| [docs/final_project_report.docx](docs/final_project_report.docx) | final project report |
| [review.md](review.md) | the plan for the final review and the demo |

## Requirements

- Linux with FUSE 3 (tested on WSL2, kernel 6.6)
- CMake 3.16 or newer and a C++17 compiler
- SQLite 3.35 or newer and libfuse 3: `libsqlite3-dev`, `libfuse3-dev`, `fuse3` on Debian or Ubuntu
- Python 3 with scikit-learn (needed by version C), and matplotlib if you want the charts

## Build and test

Create the virtual environment before running CMake, so the build finds it:

```sh
python3 -m venv .venv
.venv/bin/pip install scikit-learn matplotlib
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests take one to two minutes and should end with
`100% tests passed, 0 tests failed out of 5`.

## Quick start

```sh
build/ai_fs format disk.img disk.db 25600     # 25600 blocks of 4 KiB = 100 MiB
mkdir mnt
build/ai_fs mount disk.img disk.db mnt        # version C
cp somefile mnt/ && ls -l mnt
fusermount3 -u mnt
```

The full list of commands and options is in [docs/user_manual.md](docs/user_manual.md).

## Results in brief

Average read latency in microseconds (median of 3 runs, reads back to back):

| Workload | A | B | C |
|---|---:|---:|---:|
| sequential | 148 | 116 | 87 |
| random | 329 | 321 | 282 |
| repetitive | 347 | 195 | 125 |
| mixed | 252 | 275 | 212 |

Version C is fastest on every workload when reads come back to back, and it
wastes far fewer prefetches than B. It is weakest on the mixed workload, where
it predicts 24% of reads against B's 42%. On the random workload nothing is
prefetched, so that row is not a reliable difference. The details, and the
limits of the evaluation, are in [docs/evaluation.md](docs/evaluation.md).

To run the evaluation again (72 benchmark runs, about 8 minutes; it rewrites
everything in `experiments/`):

```sh
.venv/bin/python python/evaluate.py
```

## Layout of the source

| Path | What it is |
|---|---|
| `src/block_storage.cpp` | one image file of 4,096-byte blocks |
| `src/block_cache.cpp` | LRU cache of blocks and the prefetch threads |
| `src/filesystem.cpp` | files, directories, block allocation, the three versions |
| `src/predictor.cpp` | version B's hand-written predictor |
| `src/model_client.cpp` | version C: talks to the Python models and follows the tree |
| `src/main.cpp` | the FUSE adapter and the command line |
| `python/model.py` | version C: the two scikit-learn models |
| `python/evaluate.py` | runs the benchmark and writes the tables and charts |
| `tools/bench.cpp` | workload generator and measurement |
| `tests/` | tests for every layer, and one that mounts the filesystem |

## Known limits

- No permissions, ownership, timestamps or links.
- Not crash-safe: metadata changes are transactions, but data blocks are not journaled.
- The mount is single-threaded; only prefetch reads run in parallel.
- The access log grows without bound.
