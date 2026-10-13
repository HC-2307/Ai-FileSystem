# AI-Enhanced Database-Backed Filesystem

A Linux user-space filesystem (FUSE) that keeps file data in fixed-size blocks,
keeps metadata in SQLite, and uses a machine-learning model (a scikit-learn
decision tree, trained on the reads while the filesystem runs) to predict
which blocks will be read next and load them ahead of time. Python trains the
tree; the filesystem follows it. The full
description is in [project.md](project.md); how it works is in
[docs/architecture.md](docs/architecture.md); measured results are in
[docs/evaluation.md](docs/evaluation.md).

## Build and test

Needs CMake, a C++17 compiler, SQLite 3.35 or newer and libfuse 3
(`libsqlite3-dev`, `libfuse3-dev`, `fuse3` on Debian or Ubuntu), and Python 3
with scikit-learn for the model. Create the virtual environment before running
CMake, so the build finds it:

```sh
python3 -m venv .venv && .venv/bin/pip install scikit-learn matplotlib   # matplotlib is only for the charts
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use

```sh
build/ai_fs format disk.img disk.db 25600        # 25600 blocks of 4 KiB = 100 MiB
mkdir mnt
build/ai_fs mount disk.img disk.db mnt           # version C, the learned model; add --version A or B to compare
cp somefile mnt/ && ls -l mnt
fusermount3 -u mnt
```

`mount` also takes `--cache-mb N` (default 4), `--direct` (open the image with
`O_DIRECT`) and any FUSE option such as `-f` to stay in the foreground, where
it prints cache statistics on unmount.

## Reproduce the evaluation

```sh
.venv/bin/python python/evaluate.py
```

This runs 72 benchmark runs and rewrites everything in `experiments/`.
