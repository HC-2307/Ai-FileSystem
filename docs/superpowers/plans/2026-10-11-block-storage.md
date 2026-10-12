# Block Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tested C++ library that stores fixed-size blocks in a single image file, with an optional mode that bypasses the kernel page cache.

**Architecture:** `block_storage` wraps one POSIX file descriptor on a preallocated image file and reads or writes whole 4096-byte blocks by block number using `pread`/`pwrite`. It knows nothing about files, directories, allocation, or caching; those belong to later plans that sit on top of it. The code moves into a static library `ai_fs_core` so tests and the future FUSE adapter link the same object code.

**Tech Stack:** C++17, CMake 3.16+, CTest, POSIX file I/O (`open`, `pread`, `pwrite`, `ftruncate`, `fstat`). No third-party libraries.

**Spec:** `project.md` (sections 4.3 "Block-Based Storage", 10 "Project Scope", 11 "Phase 2")

## Scope

`project.md` describes eight subsystems. This plan covers the first only. Each later one gets its own plan, in this order:

1. **Block storage** (this plan)
2. SQLite metadata and block allocation
3. FUSE adapter
4. Access monitoring
5. Baseline cache and metadata cache (Version A)
6. Prediction and prefetching (Version B)
7. Feedback-driven adaptive policy (Version C)
8. Workload generator and evaluation

## Assumed Design Decisions

These are the recommended answers from the design interview of 2026-10-11. They were proposed but not confirmed. Each one this plan depends on is marked.

- Storage and metadata are built as a library first; FUSE is added later as a thin adapter. **(this plan depends on it)**
- File data lives in one preallocated image file of fixed 4096-byte blocks. **(this plan depends on it)**
- The kernel page cache is bypassed with `O_DIRECT` on the image file so the project's own cache is the only cache measured. **(Task 3 depends on it; drop Task 3 if rejected)**
- No crash consistency is claimed for data blocks. **(this plan depends on it: no journal, no fsync)**

## Global Constraints

- Language standard: C++17, `CMAKE_CXX_EXTENSIONS OFF`.
- Platform: Linux only.
- Block size: exactly 4096 bytes, exposed as `block_storage::BLOCK_SIZE`.
- Existing names stay: class `block_storage`, methods `create`, `open`, `writeBlock`, `readBlock`.
- No `using namespace std;` in headers. Remove it from `include/block_storage.h`.
- All file offsets are computed in `off_t` (64-bit). Never multiply a block number by 4096 in 32-bit arithmetic.
- Errors are reported by exception, never by return code:
  - `std::system_error` (carrying `errno`) when a system call fails.
  - `std::out_of_range` for a block number `>= blockCount`.
  - `std::invalid_argument` for bad arguments (zero blocks, oversized data, malformed image).
  - `std::logic_error` for I/O on a storage that is not open.
- Tests create image files in the current working directory (the build directory, ext4), never in `/tmp`. `/tmp` is tmpfs on this machine and rejects `O_DIRECT`.
- Every test removes its image file before and after it runs.
- One commit per task, made by the user (see `CLAUDE.md`). No AI attribution lines, no `feat:` style prefixes.

## Review Focus

Conditions the spec implies but does not spell out, most likely first. Each is pinned by a test in the task named.

1. **`create` on a path that already exists** must fail and leave the existing file untouched, not truncate it. (Task 1, `create_refuses_existing_file`)
2. **`open` on a file that is not a valid image** (missing, empty, or a size that is not a multiple of 4096) must throw, not report a wrong block count. (Task 1, `open_rejects_malformed_image`)
3. **Images larger than 2 GiB** must address the last block correctly; a 32-bit offset would wrap. (Task 2, `last_block_of_large_image`)
4. **Block number out of range** on read or write must throw and must not grow the file. (Task 2, `out_of_range_block_throws`)
5. **Reading or writing a storage that was never opened, or was closed**, must throw, not operate on file descriptor -1. (Task 2, `io_on_closed_storage_throws`)

---

## File Structure

| File | Responsibility |
|---|---|
| `include/block_storage.h` | Modify. The `block_storage` class declaration. |
| `src/block_storage.cpp` | Rewrite. All POSIX I/O. |
| `CMakeLists.txt` | Modify. Adds `ai_fs_core` library, test target, CTest. |
| `tests/check.h` | Create. Two assertion macros that work in release builds. |
| `tests/block_storage_test.cpp` | Create. All tests for this plan. |
| `tests/header_test.cpp` | Delete. Superseded by `block_storage_test.cpp`. |

Test image files are named `test_<testname>.img` so the existing `*.img` ignore rule covers them.

---

### Task 1: Build scaffolding, `create`, `open`, `close`

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `include/block_storage.h`
- Rewrite: `src/block_storage.cpp`
- Create: `tests/check.h`
- Create: `tests/block_storage_test.cpp`
- Delete: `tests/header_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
// include/block_storage.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class block_storage {
public:
    static constexpr std::size_t BLOCK_SIZE = 4096;

    block_storage() = default;
    ~block_storage();                                   // calls close()
    block_storage(const block_storage&) = delete;
    block_storage& operator=(const block_storage&) = delete;

    void create(const std::string& filename, std::uint32_t blockCount);
    void open(const std::string& filename);
    void close();                                       // safe to call twice
    bool isOpen() const;
    std::uint32_t getBlockCount() const;                // 0 when not open

private:
    int fd = -1;
    std::uint32_t blockCount = 0;
};
```

```cpp
// tests/check.h
// CHECK(cond): on failure print "file:line: CHECK failed: cond" to stderr and std::exit(1).
// CHECK_THROWS(expr, ExceptionType): fail unless expr throws ExceptionType.
```

- [ ] **Step 1: Restructure `CMakeLists.txt`**

Targets after this step:
- `ai_fs_core`: static library from `src/block_storage.cpp`, with `include/` as a `PUBLIC` include directory.
- `ai_fs`: executable from `src/main.cpp`, links `ai_fs_core`.
- `block_storage_test`: executable from `tests/block_storage_test.cpp`, links `ai_fs_core`, adds `tests/` as a private include directory.
- `enable_testing()` and `add_test(NAME block_storage_test COMMAND block_storage_test)`.
- Compile options `-Wall -Wextra` on all three targets.

- [ ] **Step 2: Write `tests/check.h`** with the two macros described above.

- [ ] **Step 3: Write the failing tests** in `tests/block_storage_test.cpp`

Each test is a `static void` function called from `main`; `main` prints the test name before calling it and returns 0 at the end. Use `std::remove` to delete image files.

```cpp
static void create_makes_image_of_exact_size() {
    const char* path = "test_create.img";
    std::remove(path);
    block_storage s;
    s.create(path, 16);
    CHECK(s.isOpen());
    CHECK(s.getBlockCount() == 16);
    s.close();
    CHECK(!s.isOpen());
    CHECK(s.getBlockCount() == 0);
    CHECK(std::filesystem::file_size(path) == 16 * 4096);
    std::remove(path);
}

static void open_reads_block_count_from_file_size() {
    const char* path = "test_open.img";
    std::remove(path);
    { block_storage s; s.create(path, 8); }      // destructor closes
    block_storage s;
    s.open(path);
    CHECK(s.getBlockCount() == 8);
    std::remove(path);
}

static void create_refuses_existing_file() {
    const char* path = "test_exists.img";
    std::remove(path);
    { block_storage s; s.create(path, 4); }
    block_storage s;
    CHECK_THROWS(s.create(path, 2), std::system_error);
    CHECK(std::filesystem::file_size(path) == 4 * 4096);   // untouched
    std::remove(path);
}

static void create_rejects_zero_blocks() {
    block_storage s;
    CHECK_THROWS(s.create("test_zero.img", 0), std::invalid_argument);
    CHECK(!std::filesystem::exists("test_zero.img"));
}

static void open_rejects_malformed_image() {
    block_storage s;
    CHECK_THROWS(s.open("test_missing.img"), std::system_error);

    const char* path = "test_bad.img";
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); }            // empty
    CHECK_THROWS(s.open(path), std::invalid_argument);
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); f << "abc"; } // 3 bytes
    CHECK_THROWS(s.open(path), std::invalid_argument);
    CHECK(!s.isOpen());
    std::remove(path);
}

static void second_open_on_open_storage_throws() {
    const char* path = "test_twice.img";
    std::remove(path);
    block_storage s;
    s.create(path, 2);
    CHECK_THROWS(s.open(path), std::logic_error);
    std::remove(path);
}
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build`
Expected: build FAILS with errors such as "no member named 'isOpen'" (the header still has the old interface).

- [ ] **Step 5: Replace `include/block_storage.h`** with the declaration in the Interfaces block above.

- [ ] **Step 6: Implement in `src/block_storage.cpp`**

- `create`: reject `blockCount == 0` before touching the filesystem; `::open` with `O_RDWR | O_CREAT | O_EXCL`, mode `0644`; size the file with `ftruncate` to `off_t(blockCount) * BLOCK_SIZE` (sparse, unwritten blocks read back as zeros). If `ftruncate` fails, close the descriptor and `unlink` the file before throwing.
- `open`: `::open` with `O_RDWR`; get the size with `fstat`; throw `std::invalid_argument` and close the descriptor if the size is 0 or not a multiple of `BLOCK_SIZE`.
- Both throw `std::logic_error` if the storage is already open.
- `close`: no-op when `fd == -1`; otherwise `::close`, then reset `fd` to -1 and `blockCount` to 0.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`

- [ ] **Step 8: Commit**

```bash
git rm --cached tests/header_test.cpp 2>/dev/null; rm -f tests/header_test.cpp
git add CMakeLists.txt include/block_storage.h src/block_storage.cpp tests/check.h tests/block_storage_test.cpp
git commit -m "block storage create/open/close with CTest scaffolding"
```

---

### Task 2: `readBlock` and `writeBlock`

**Files:**
- Modify: `include/block_storage.h`
- Modify: `src/block_storage.cpp`
- Test: `tests/block_storage_test.cpp`

**Interfaces:**
- Consumes: `block_storage::create`, `open`, `close`, `isOpen`, `getBlockCount`, `BLOCK_SIZE` from Task 1.
- Produces:

```cpp
// added to class block_storage, public:
void writeBlock(std::uint32_t blockNumber, const std::vector<char>& data);
std::vector<char> readBlock(std::uint32_t blockNumber);   // always BLOCK_SIZE bytes
```

- [ ] **Step 1: Write the failing tests**

```cpp
static void write_then_read_round_trips() {
    const char* path = "test_rw.img";
    std::remove(path);
    block_storage s;
    s.create(path, 4);
    std::vector<char> data(4096);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = char(i % 251);
    s.writeBlock(2, data);
    CHECK(s.readBlock(2) == data);
    std::remove(path);
}

static void unwritten_block_reads_as_zeros() {
    const char* path = "test_zeros.img";
    std::remove(path);
    block_storage s;
    s.create(path, 4);
    CHECK(s.readBlock(3) == std::vector<char>(4096, 0));
    std::remove(path);
}

static void short_write_is_zero_padded() {
    const char* path = "test_pad.img";
    std::remove(path);
    block_storage s;
    s.create(path, 2);
    s.writeBlock(0, std::vector<char>(4096, 'x'));     // fill first
    s.writeBlock(0, {'a', 'b', 'c'});                  // then overwrite short
    std::vector<char> expected(4096, 0);
    expected[0] = 'a'; expected[1] = 'b'; expected[2] = 'c';
    CHECK(s.readBlock(0) == expected);
    std::remove(path);
}

static void oversized_write_throws() {
    const char* path = "test_big.img";
    std::remove(path);
    block_storage s;
    s.create(path, 2);
    CHECK_THROWS(s.writeBlock(0, std::vector<char>(4097, 'x')), std::invalid_argument);
    std::remove(path);
}

static void writes_do_not_touch_neighbours() {
    const char* path = "test_neigh.img";
    std::remove(path);
    block_storage s;
    s.create(path, 3);
    s.writeBlock(0, std::vector<char>(4096, 'a'));
    s.writeBlock(2, std::vector<char>(4096, 'c'));
    s.writeBlock(1, std::vector<char>(4096, 'b'));
    CHECK(s.readBlock(0) == std::vector<char>(4096, 'a'));
    CHECK(s.readBlock(2) == std::vector<char>(4096, 'c'));
    std::remove(path);
}

static void data_survives_reopen() {
    const char* path = "test_persist.img";
    std::remove(path);
    { block_storage s; s.create(path, 4); s.writeBlock(1, std::vector<char>(4096, 'z')); }
    block_storage s;
    s.open(path);
    CHECK(s.readBlock(1) == std::vector<char>(4096, 'z'));
    std::remove(path);
}

static void out_of_range_block_throws() {
    const char* path = "test_range.img";
    std::remove(path);
    block_storage s;
    s.create(path, 4);
    CHECK_THROWS(s.readBlock(4), std::out_of_range);
    CHECK_THROWS(s.writeBlock(4, std::vector<char>(4096, 'x')), std::out_of_range);
    CHECK_THROWS(s.readBlock(0xFFFFFFFFu), std::out_of_range);
    CHECK(std::filesystem::file_size(path) == 4 * 4096);   // did not grow
    std::remove(path);
}

static void io_on_closed_storage_throws() {
    block_storage never_opened;
    CHECK_THROWS(never_opened.readBlock(0), std::logic_error);

    const char* path = "test_closed.img";
    std::remove(path);
    block_storage s;
    s.create(path, 2);
    s.close();
    CHECK_THROWS(s.readBlock(0), std::logic_error);
    CHECK_THROWS(s.writeBlock(0, {}), std::logic_error);
    std::remove(path);
}

static void last_block_of_large_image() {
    const char* path = "test_large.img";
    std::remove(path);
    const std::uint32_t blocks = 600000;               // 2.29 GiB, sparse on disk
    block_storage s;
    s.create(path, blocks);
    s.writeBlock(blocks - 1, std::vector<char>(4096, 'L'));
    CHECK(s.readBlock(blocks - 1) == std::vector<char>(4096, 'L'));
    CHECK(s.readBlock(0) == std::vector<char>(4096, 0));
    CHECK(std::filesystem::file_size(path) == std::uintmax_t(blocks) * 4096);
    std::remove(path);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build`
Expected: build FAILS with "no member named 'readBlock'".

- [ ] **Step 3: Implement `readBlock` and `writeBlock`**

Check order in both: not open (`std::logic_error`), then block number (`std::out_of_range`), then data size (`std::invalid_argument`, write only).

Both go through one private pair of helpers that loop until all 4096 bytes are transferred, because `pread` and `pwrite` may transfer fewer bytes than asked and may fail with `EINTR`:

```cpp
// private, in block_storage.cpp
// Retry on EINTR; advance on a short transfer; throw std::system_error on any other error.
// A pread that returns 0 before the block is complete is an error (file shorter than expected).
// `direct` is false until Task 3 wires the I/O mode through.
static void readFully(int fd, char* buf, off_t offset, bool direct);    // exactly BLOCK_SIZE bytes
static void writeFully(int fd, const char* buf, off_t offset, bool direct);
```

`writeBlock` copies `data` into a zero-filled 4096-byte buffer and writes the buffer, so a short `data` is padded. Declare that buffer, and the read buffer in `readBlock`, as `alignas(block_storage::BLOCK_SIZE) char buf[block_storage::BLOCK_SIZE]` on the stack; Task 3 requires the alignment.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`

- [ ] **Step 5: Commit**

```bash
git add include/block_storage.h src/block_storage.cpp tests/block_storage_test.cpp
git commit -m "block storage readBlock/writeBlock"
```

---

### Task 3: Direct I/O mode

Depends on the unconfirmed decision to bypass the kernel page cache. Skip this task if that decision is rejected.

**Files:**
- Modify: `include/block_storage.h`
- Modify: `src/block_storage.cpp`
- Test: `tests/block_storage_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2, including the 4096-aligned stack buffers.
- Produces (replaces the Task 1 signatures; the default keeps every existing call compiling):

```cpp
enum class io_mode { buffered, direct };

void create(const std::string& filename, std::uint32_t blockCount, io_mode mode = io_mode::buffered);
void open(const std::string& filename, io_mode mode = io_mode::buffered);
io_mode getMode() const;      // io_mode::buffered when not open
```

`io_mode` is declared at namespace scope in `include/block_storage.h`, above the class.

- [ ] **Step 1: Write the failing tests**

```cpp
static void direct_mode_round_trips() {
    const char* path = "test_direct.img";
    std::remove(path);
    block_storage s;
    s.create(path, 4, io_mode::direct);
    CHECK(s.getMode() == io_mode::direct);
    std::vector<char> data(4096, 'd');
    s.writeBlock(3, data);
    CHECK(s.readBlock(3) == data);
    CHECK(s.readBlock(0) == std::vector<char>(4096, 0));
    std::remove(path);
}

static void direct_and_buffered_see_the_same_data() {
    const char* path = "test_mixed.img";
    std::remove(path);
    { block_storage s; s.create(path, 2, io_mode::direct); s.writeBlock(1, std::vector<char>(4096, 'm')); }
    { block_storage s; s.open(path);                    CHECK(s.readBlock(1) == std::vector<char>(4096, 'm')); }
    { block_storage s; s.open(path, io_mode::direct);   CHECK(s.readBlock(1) == std::vector<char>(4096, 'm')); }
    std::remove(path);
}

static void mode_defaults_to_buffered() {
    const char* path = "test_mode.img";
    std::remove(path);
    block_storage s;
    CHECK(s.getMode() == io_mode::buffered);
    s.create(path, 1);
    CHECK(s.getMode() == io_mode::buffered);
    std::remove(path);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build`
Expected: build FAILS with "'io_mode' has not been declared".

- [ ] **Step 3: Implement**

Add `O_DIRECT` to the `::open` flags when `mode == io_mode::direct`. `O_DIRECT` needs `_GNU_SOURCE` on glibc; g++ defines it by default, so no extra define is required. Store the mode in a private member and reset it to `buffered` in `close`. No change to `readBlock`/`writeBlock`: the buffers are already 4096-aligned and every transfer is one whole block at a block-aligned offset.

A short direct-I/O transfer leaves an unaligned remainder that the retry loop cannot continue. In `readFully`/`writeFully`, when the mode is direct and a transfer returns fewer than 4096 bytes, throw `std::system_error` with `EIO` instead of retrying.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`

If `direct_mode_round_trips` fails with `EINVAL`, the build directory is on a filesystem without `O_DIRECT` support (tmpfs, some network mounts). Move the build directory to ext4; do not weaken the test.

- [ ] **Step 5: Commit**

```bash
git add include/block_storage.h src/block_storage.cpp tests/block_storage_test.cpp
git commit -m "optional O_DIRECT mode for block storage"
```

---

## Out of Scope for This Plan

- Tracking which blocks are free or which file owns a block (plan 2, SQLite).
- Caching, prefetching, access logging (plans 4 to 7).
- Thread safety beyond what `pread`/`pwrite` give for free. Concurrent calls on distinct blocks are safe; `create`/`open`/`close` must not race with I/O.
- `fsync` and crash consistency.
- Growing or shrinking an image after creation.
