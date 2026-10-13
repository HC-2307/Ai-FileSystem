# User manual

How to install, run and use the filesystem.

## 1. Install

You need Linux with FUSE 3. On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake libsqlite3-dev libfuse3-dev fuse3 python3-venv
```

Then, in the project folder:

```sh
python3 -m venv .venv
.venv/bin/pip install scikit-learn matplotlib
cmake -S . -B build
cmake --build build
```

Create `.venv` before running `cmake`. The build looks for
`.venv/bin/python` and uses it to run the models in version C. If there is no
`.venv`, it uses `python3`, which must then have scikit-learn installed.
matplotlib is only needed for the charts.

This builds two programs in `build/`:

| Program | What it is for |
|---|---|
| `ai_fs` | create and mount a filesystem |
| `ai_fs_bench` | run one benchmark workload and print the measurements |

## 2. Create a filesystem

```sh
build/ai_fs format <image> <db> <blocks>
```

| Argument | Meaning |
|---|---|
| `<image>` | file to create for the file data; it must not exist yet |
| `<db>` | file to create for the SQLite metadata |
| `<blocks>` | size in blocks of 4,096 bytes, a whole number above 0 |

Example: 100 MiB.

```sh
build/ai_fs format disk.img disk.db 25600
```

Keep the two files together. One is no use without the other.

## 3. Mount it

```sh
mkdir mnt
build/ai_fs mount <image> <db> <mountpoint> [options]
```

| Option | Meaning |
|---|---|
| `--version A` | LRU caches only, no prediction |
| `--version B` | hand-written prediction |
| `--version C` | the machine-learning models (the default) |
| `--cache-mb N` | memory for the two caches together, in MiB (default 4) |
| `--direct` | open the image with `O_DIRECT`, bypassing the kernel's own cache; not available on tmpfs |
| `-f` | stay in the foreground; statistics are printed when it is unmounted |

Any other option is passed to FUSE.

Example:

```sh
build/ai_fs mount disk.img disk.db mnt --cache-mb 16
```

Without `-f` the program goes to the background and the command returns at
once.

## 4. Use it

The mount point is an ordinary folder. These work:

```sh
cp report.pdf mnt/
mkdir mnt/notes
echo hello > mnt/notes/a.txt
cat mnt/notes/a.txt
mv mnt/notes/a.txt mnt/notes/b.txt
rm mnt/notes/b.txt
rmdir mnt/notes
ls -l mnt
```

What is supported: creating, reading, writing, truncating, renaming and
deleting files; creating, listing and removing directories.

What is not: permissions, ownership, timestamps (every date shows as
1 January 1970), symbolic links and hard links.

## 5. Unmount

```sh
fusermount3 -u mnt
```

If it was mounted with `-f`, one line of statistics is printed, for example:

```
ai_fs: block reads 3908, cache hits 3807, disk reads 3908, disk writes 1955,
prefetched 3812 (used 3807, wasted 0), mapping queries 1956
```

| Field | Meaning |
|---|---|
| block reads | blocks asked for by programs |
| cache hits | of those, how many were already in memory |
| disk reads / disk writes | blocks read from and written to the image |
| prefetched | blocks loaded ahead of time |
| used / wasted | prefetched blocks that were then read, or dropped unread |
| mapping queries | lookups of "which image block holds this file block" that reached SQLite |

## 6. Run the tests

```sh
ctest --test-dir build --output-on-failure
```

Five test suites run. `mount_test` mounts a real filesystem and is reported as
skipped where FUSE is not available.

## 7. Run the benchmark

One workload, one version:

```sh
cd build
./ai_fs_bench <A|B|C> <sequential|random|repetitive|mixed> [cacheKb] [thinkUs] [seed]
./ai_fs_bench header
```

| Argument | Meaning | Default |
|---|---|---|
| version | A, B or C | none |
| workload | which pattern of reads to replay | none |
| `cacheKb` | cache memory in KiB | 1024 |
| `thinkUs` | pause between operations, in microseconds | 0 |
| `seed` | seed for the random parts of the workload | 1 |

It prints one line of comma-separated values; `header` prints the column
names. To see the main columns side by side:

```sh
(./ai_fs_bench header; for v in A B C; do ./ai_fs_bench $v sequential; done) \
  | cut -d, -f1,2,8,12,15,17 | column -s, -t
```

Run it from a folder on a real disk (not tmpfs): it opens its image with
`O_DIRECT`. It creates and removes two temporary files there.

The whole evaluation (72 runs, about 8 minutes):

```sh
.venv/bin/python python/evaluate.py
```

It rewrites `experiments/results.csv`, `experiments/summary.md` and the charts.
Do not run anything heavy at the same time; the timings are sensitive to it.

## 8. Messages and what to do

| Message | Cause | What to do |
|---|---|---|
| `usage: ai_fs format ...` | wrong or missing arguments | check the command against sections 2 and 3 |
| `ai_fs: model: could not start ...` | version C could not start the Python models | create `.venv` with scikit-learn and build again, or mount with `--version A` or `B` |
| `... is not a filesystem database` | the `<db>` file was not made by `format` | use the database that was created with this image |
| `No space left on device` | the image is full | delete files, or create a larger image |
| `No such file or directory` | the path does not exist | check the path |
| `File exists` | creating something that is already there | choose another name |
| `Directory not empty` | removing a directory that has entries | remove its contents first |
| `Is a directory` / `Not a directory` | a file operation on a directory, or the reverse | check the path |
| `Transport endpoint is not connected` | the mount point is left over from a filesystem that stopped | `fusermount3 -u <mountpoint>` |
| the mount command returns but the folder is empty | the filesystem stopped at start-up | mount again with `-f` to see the error |
