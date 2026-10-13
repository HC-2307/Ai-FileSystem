#include "filesystem.h"
#include "check.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <sqlite3.h>

#define CHECK_ERRNO(expr, want)                                            \
    do {                                                                   \
        int got = 0;                                                       \
        try {                                                              \
            expr;                                                          \
        } catch (const std::system_error& e) {                             \
            got = e.code().value();                                        \
        }                                                                  \
        if (got != (want)) {                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " << #expr      \
                      << " gave errno " << got << ", wanted " << #want     \
                      << std::endl;                                        \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

static const char* IMG = "test_fs.img";
static const char* DB = "test_fs.db";
static fs_options OPTS;   // every test runs once per version

static void cleanup() {
    std::remove(IMG);
    std::remove(DB);
}

// Fresh filesystem of `blocks` blocks, formatted into `fs`.
static void fresh(filesystem& fs, std::uint32_t blocks = 16) {
    cleanup();
    fs.format(IMG, DB, blocks, OPTS);
}

static std::vector<char> bytes(std::size_t n, char c) {
    return std::vector<char>(n, c);
}

static void format_creates_empty_root() {
    filesystem fs;
    fresh(fs);
    CHECK(fs.stat("/").isDir);
    CHECK(fs.list("/").empty());
}

static void format_refuses_existing_image() {
    { filesystem fs; fresh(fs); fs.createFile("/keep"); }
    filesystem fs;
    CHECK_ERRNO(fs.format(IMG, DB, 16), EEXIST);
    fs.open(IMG, DB, OPTS);
    CHECK(fs.list("/") == std::vector<std::string>{"keep"});   // untouched
}

static void create_file_appears_in_listing() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    CHECK(!fs.stat("/a").isDir);
    CHECK(fs.stat("/a").size == 0);
    CHECK(fs.list("/") == std::vector<std::string>{"a"});
}

static void create_rejects_bad_paths() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    CHECK_ERRNO(fs.createFile("/a"), EEXIST);
    CHECK_ERRNO(fs.mkdir("/a"), EEXIST);
    CHECK_ERRNO(fs.createFile("/"), EEXIST);
    CHECK_ERRNO(fs.createFile("/missing/b"), ENOENT);
    CHECK_ERRNO(fs.createFile("/a/b"), ENOTDIR);
    CHECK_ERRNO(fs.createFile("relative"), EINVAL);
    CHECK_ERRNO(fs.stat("/nope"), ENOENT);
}

static void directories_nest() {
    filesystem fs;
    fresh(fs);
    fs.mkdir("/d");
    fs.mkdir("/d/e");
    fs.createFile("/d/e/f");
    fs.createFile("/d/b");
    CHECK(fs.stat("/d/e").isDir);
    CHECK(fs.list("/d") == (std::vector<std::string>{"b", "e"}));
    CHECK(fs.list("/d/e") == std::vector<std::string>{"f"});
    CHECK_ERRNO(fs.list("/d/b"), ENOTDIR);
}

static void write_then_read_round_trips() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    fs.write("/a", 0, {'h', 'e', 'l', 'l', 'o'});
    CHECK(fs.stat("/a").size == 5);
    CHECK(fs.read("/a", 0, 5) == (std::vector<char>{'h', 'e', 'l', 'l', 'o'}));
    CHECK(fs.read("/a", 1, 3) == (std::vector<char>{'e', 'l', 'l'}));
}

static void write_spans_blocks_at_unaligned_offset() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    std::vector<char> data(5000);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = char(i % 251);
    fs.write("/a", 4000, data);
    CHECK(fs.stat("/a").size == 9000);
    CHECK(fs.read("/a", 4000, 5000) == data);
    CHECK(fs.read("/a", 0, 4000) == bytes(4000, 0));   // hole before the data
}

static void read_is_clamped_to_file_size() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    fs.write("/a", 0, bytes(10, 'x'));
    CHECK(fs.read("/a", 4, 100) == bytes(6, 'x'));
    CHECK(fs.read("/a", 10, 100).empty());
    CHECK(fs.read("/a", 500, 100).empty());
}

static void hole_reads_as_zeros() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    fs.write("/a", 3 * 4096, bytes(1, 'z'));
    CHECK(fs.read("/a", 4096, 4096) == bytes(4096, 0));
}

static void overwrite_keeps_surrounding_bytes() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    fs.write("/a", 0, bytes(8192, 'a'));
    fs.write("/a", 4090, bytes(12, 'b'));
    std::vector<char> expected = bytes(8192, 'a');
    for (int i = 4090; i < 4102; ++i) expected[i] = 'b';
    CHECK(fs.read("/a", 0, 8192) == expected);
    CHECK(fs.stat("/a").size == 8192);
}

static void files_do_not_share_blocks() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/a");
    fs.createFile("/b");
    fs.write("/a", 0, bytes(4096, 'a'));
    fs.write("/b", 0, bytes(4096, 'b'));
    fs.write("/a", 4096, bytes(4096, 'A'));
    CHECK(fs.read("/a", 0, 8192) == [] { auto v = bytes(8192, 'a'); std::fill(v.begin() + 4096, v.end(), 'A'); return v; }());
    CHECK(fs.read("/b", 0, 4096) == bytes(4096, 'b'));
}

static void remove_frees_blocks_for_reuse() {
    filesystem fs;
    fresh(fs, 4);
    fs.createFile("/a");
    fs.write("/a", 0, bytes(4 * 4096, 'a'));
    fs.createFile("/b");
    CHECK_ERRNO(fs.write("/b", 0, bytes(1, 'b')), ENOSPC);
    fs.remove("/a");
    CHECK_ERRNO(fs.stat("/a"), ENOENT);
    fs.write("/b", 0, bytes(4 * 4096, 'b'));
    CHECK(fs.read("/b", 0, 4 * 4096) == bytes(4 * 4096, 'b'));
}

static void reused_block_does_not_leak_old_data() {
    filesystem fs;
    fresh(fs, 1);
    fs.createFile("/old");
    fs.write("/old", 0, bytes(4096, 'S'));
    fs.remove("/old");
    fs.createFile("/new");
    fs.write("/new", 0, bytes(10, 'n'));
    fs.write("/new", 4095, bytes(1, 'n'));
    std::vector<char> expected = bytes(4096, 0);
    for (int i = 0; i < 10; ++i) expected[i] = 'n';
    expected[4095] = 'n';
    CHECK(fs.read("/new", 0, 4096) == expected);
}

static void failed_write_leaves_metadata_unchanged() {
    filesystem fs;
    fresh(fs, 4);
    fs.createFile("/a");
    CHECK_ERRNO(fs.write("/a", 0, bytes(5 * 4096, 'x')), ENOSPC);
    CHECK(fs.stat("/a").size == 0);
    fs.write("/a", 0, bytes(4 * 4096, 'y'));           // all 4 blocks still free
    CHECK(fs.read("/a", 0, 4 * 4096) == bytes(4 * 4096, 'y'));
}

static void remove_directory_rules() {
    filesystem fs;
    fresh(fs);
    fs.mkdir("/d");
    fs.createFile("/d/f");
    CHECK_ERRNO(fs.remove("/d"), ENOTEMPTY);
    fs.remove("/d/f");
    fs.remove("/d");
    CHECK(fs.list("/").empty());
    CHECK_ERRNO(fs.remove("/d"), ENOENT);
    CHECK_ERRNO(fs.remove("/"), EBUSY);
}

static void directories_are_not_files() {
    filesystem fs;
    fresh(fs);
    fs.mkdir("/d");
    CHECK_ERRNO(fs.write("/d", 0, bytes(1, 'x')), EISDIR);
    CHECK_ERRNO(fs.read("/d", 0, 1), EISDIR);
}

static void contents_survive_reopen() {
    {
        filesystem fs;
        fresh(fs);
        fs.mkdir("/d");
        fs.createFile("/d/f");
        fs.write("/d/f", 100, bytes(6000, 'p'));
    }
    filesystem fs;
    fs.open(IMG, DB, OPTS);
    CHECK(fs.stat("/d/f").size == 6100);
    CHECK(fs.read("/d/f", 100, 6000) == bytes(6000, 'p'));
}

static void open_rejects_missing_database() {
    { filesystem fs; fresh(fs); }
    std::remove(DB);
    filesystem fs;
    CHECK_THROWS(fs.open(IMG, DB, OPTS), std::exception);
    CHECK_THROWS(fs.stat("/"), std::logic_error);       // still closed
    FILE* stray = std::fopen(DB, "r");                  // open must not create it
    CHECK(stray == nullptr);
}

static void truncate_shrinks_and_frees_blocks() {
    filesystem fs;
    fresh(fs, 4);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(4 * 4096, 'a'));
    fs.truncate("/f", 5000);
    CHECK(fs.stat("/f").size == 5000);
    CHECK(fs.read("/f", 0, 9000) == bytes(5000, 'a'));
    fs.createFile("/g");
    fs.write("/g", 0, bytes(2 * 4096, 'b'));               // the two freed blocks
    CHECK_ERRNO(fs.write("/g", 2 * 4096, bytes(1, 'b')), ENOSPC);
    CHECK(fs.read("/f", 0, 9000) == bytes(5000, 'a'));
}

static void truncate_then_grow_reads_zeros() {
    filesystem fs;
    fresh(fs);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(6000, 'a'));
    fs.truncate("/f", 100);
    fs.truncate("/f", 6000);
    std::vector<char> want = bytes(100, 'a');
    want.resize(6000, 0);
    CHECK(fs.read("/f", 0, 6000) == want);
    fs.truncate("/f", 0);
    CHECK(fs.read("/f", 0, 10).empty());
    CHECK_ERRNO(fs.truncate("/", 0), EISDIR);
}

static void rename_moves_and_replaces() {
    filesystem fs;
    fresh(fs, 4);
    fs.mkdir("/d");
    fs.createFile("/a");
    fs.write("/a", 0, bytes(5000, 'a'));
    fs.rename("/a", "/d/b");
    CHECK_ERRNO(fs.stat("/a"), ENOENT);
    CHECK(fs.read("/d/b", 0, 5000) == bytes(5000, 'a'));

    fs.createFile("/c");
    fs.write("/c", 0, bytes(2 * 4096, 'c'));               // image is now full
    fs.rename("/d/b", "/c");                               // replaces /c and frees its blocks
    CHECK(fs.read("/c", 0, 5000) == bytes(5000, 'a'));
    CHECK(fs.list("/d").empty());
    fs.createFile("/e");
    fs.write("/e", 0, bytes(2 * 4096, 'e'));
    fs.rename("/c", "/c");
    CHECK(fs.stat("/c").size == 5000);
}

static void rename_rules() {
    filesystem fs;
    fresh(fs);
    fs.mkdir("/d");
    fs.mkdir("/d/sub");
    fs.mkdir("/full");
    fs.createFile("/full/x");
    fs.createFile("/f");
    CHECK_ERRNO(fs.rename("/missing", "/y"), ENOENT);
    CHECK_ERRNO(fs.rename("/d", "/d/sub/d"), EINVAL);      // into itself
    CHECK_ERRNO(fs.rename("/d", "/f"), ENOTDIR);
    CHECK_ERRNO(fs.rename("/f", "/d"), EISDIR);
    CHECK_ERRNO(fs.rename("/d", "/full"), ENOTEMPTY);
    CHECK_ERRNO(fs.rename("/f", "/f/g"), ENOTDIR);
    CHECK_ERRNO(fs.rename("/", "/r"), EBUSY);
    fs.rename("/d", "/full/d");
    CHECK(fs.stat("/full/d/sub").isDir);
}

// Random reads, writes and truncates with a cache far smaller than the data,
// checked against plain in-memory copies of the files.
static void contents_match_a_model_under_cache_pressure() {
    fs_options small = OPTS;
    small.cacheBytes = 32 * 1024;
    cleanup();
    filesystem fs;
    fs.format(IMG, DB, 256, small);
    std::vector<std::string> names = {"/a", "/b", "/c"};
    std::map<std::string, std::vector<char>> model;
    for (const std::string& name : names) fs.createFile(name);

    std::mt19937 rng(7);
    for (int i = 0; i < 600; i++) {
        const std::string& name = names[rng() % names.size()];
        std::vector<char>& want = model[name];
        const int op = rng() % 20;
        if (op == 0) {
            std::size_t size = rng() % (60 * 4096);
            fs.truncate(name, size);
            want.resize(size, 0);
        } else if (op < 6) {
            std::size_t offset = rng() % (60 * 4096), n = 1 + rng() % 9000;
            std::vector<char> data(n, char('a' + i % 26));
            fs.write(name, offset, data);
            if (want.size() < offset + n) want.resize(offset + n, 0);
            std::copy(data.begin(), data.end(), want.begin() + offset);
        } else {
            // Mostly walk forward so the predictor has something to find.
            std::size_t offset = op < 16 ? std::size_t(i % 60) * 4096 : rng() % (64 * 4096), n = 1 + rng() % 9000;
            std::vector<char> expect;
            if (offset < want.size())
                expect.assign(want.begin() + offset, want.begin() + std::min(want.size(), offset + n));
            CHECK(fs.read(name, offset, n) == expect);
        }
    }
    fs.close();
    fs.open(IMG, DB, small);
    for (const std::string& name : names) CHECK(fs.read(name, 0, 64 * 4096) == model[name]);
}

static void sequential_reads_are_prefetched() {
    filesystem fs;
    fresh(fs, 64);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(48 * 4096, 'a'));
    fs.close();
    fs.open(IMG, DB, OPTS);                                // empty cache
    for (int i = 0; i < 48; i++) CHECK(fs.read("/f", i * 4096, 4096) == bytes(4096, 'a'));
    fs_stats s = fs.stats();
    CHECK(s.cache.hits + s.cache.misses == 48);
    if (OPTS.version == fs_version::baseline) {
        CHECK(s.cache.prefetchIssued == 0);
        CHECK(s.cache.misses == 48);
    } else {
        CHECK(s.cache.prefetchUsed > 24);
        CHECK(s.cache.misses < 24);
    }
}

static void model_runs_only_in_version_c() {
    filesystem fs;
    fresh(fs, 64);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(48 * 4096, 'a'));
    fs.close();
    fs.open(IMG, DB, OPTS);
    for (int i = 0; i < 48; i++) fs.read("/f", i * 4096, 4096);
    const fs_stats s = fs.stats();
    if (OPTS.version == fs_version::adaptive) {
        CHECK(s.modelUpdates == 48);
        CHECK(s.modelAccuracy > 0.15);
    } else {
        CHECK(s.modelUpdates == 0 && s.modelAccuracy == 0);
    }
}

// The model predicts blocks past the end of a file, and of a file that is then deleted.
static void predictions_for_missing_blocks_are_ignored() {
    filesystem fs;
    fresh(fs, 64);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(48 * 4096, 'a'));
    fs.createFile("/g");
    fs.write("/g", 0, bytes(4096, 'g'));
    for (int i = 0; i < 48; i++) CHECK(fs.read("/f", i * 4096, 4096) == bytes(4096, 'a'));
    fs.remove("/f");
    CHECK(fs.read("/g", 0, 4096) == bytes(4096, 'g'));
    CHECK(fs.read("/g", 4096, 4096).empty());
}

static void random_reads_are_not_prefetched_by_the_model() {
    if (OPTS.version != fs_version::adaptive) return;
    fs_options o = OPTS;
    o.cacheBytes = 64 * 4096;
    cleanup();
    filesystem fs;
    fs.format(IMG, DB, 2100, o);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(2000 * 4096, 'a'));
    fs.close();
    fs.open(IMG, DB, o);
    std::uint32_t x = 12345;
    for (int i = 0; i < 4000; i++) {
        x = x * 1664525u + 1013904223u;
        fs.read("/f", std::uint64_t((x >> 16) % 2000) * 4096, 4096);
    }
    CHECK(fs.stats().cache.prefetchIssued < 40);
}

static void model_learns_recorded_history_at_open() {
    fs_options recorder = OPTS;
    recorder.version = fs_version::baseline;
    cleanup();
    {
        filesystem fs;
        fs.format(IMG, DB, 64, recorder);
        fs.createFile("/f");
        fs.write("/f", 0, bytes(48 * 4096, 'a'));
        for (int i = 0; i < 48; i++) fs.read("/f", i * 4096, 4096);
    }
    filesystem fs;
    fs.open(IMG, DB, OPTS);
    const fs_stats s = fs.stats();
    CHECK(s.modelUpdates == (OPTS.version == fs_version::adaptive ? 48u : 0u));
    CHECK(s.cache.prefetchIssued == 0);     // replaying history must not touch the disk
}

static void a_model_that_cannot_start_is_reported() {
    if (OPTS.version != fs_version::adaptive) return;
    { filesystem fs; fresh(fs); }
    fs_options broken = OPTS;
    broken.modelCommand = "exit 3";
    filesystem fs;
    CHECK_THROWS(fs.open(IMG, DB, broken), std::runtime_error);
    fs.open(IMG, DB, OPTS);                 // and the filesystem object is still usable
    CHECK(fs.list("/").empty());
}

static void reads_still_work_when_the_model_dies() {
    if (OPTS.version != fs_version::adaptive) return;
    fs_options dying = OPTS;
    dying.modelCommand = "echo ready; read line; exit 0";   // answers nothing, then goes away
    dying.modelWaitMs = 0;
    cleanup();
    filesystem fs;
    fs.format(IMG, DB, 64, dying);
    fs.createFile("/f");
    fs.write("/f", 0, bytes(16 * 4096, 'a'));
    for (int i = 0; i < 16; i++) CHECK(fs.read("/f", i * 4096, 4096) == bytes(4096, 'a'));
    const fs_stats s = fs.stats();
    CHECK(s.cache.prefetchIssued == 0 && s.modelUpdates == 0);
}

// Random reads of more data than either cache holds, so both caches miss.
static fs_stats afterCachePressure(const fs_options& o) {
    cleanup();
    filesystem fs;
    fs.format(IMG, DB, 600, o);
    for (const char* name : {"/a", "/b", "/c", "/d"}) {
        fs.createFile(name);
        fs.write(name, 0, bytes(128 * 4096, 'a'));
    }
    const char* names[] = {"/a", "/b", "/c", "/d"};
    std::uint32_t x = 99;
    for (int i = 0; i < 2000; i++) {
        x = x * 1664525u + 1013904223u;
        CHECK(fs.read(names[(x >> 8) % 4], std::uint64_t((x >> 16) % 128) * 4096, 4096) == bytes(4096, 'a'));
    }
    return fs.stats();
}

static void memory_split_is_learned_only_in_version_c() {
    fs_options o = OPTS;
    o.cacheBytes = 64 * 4096;
    const fs_stats s = afterCachePressure(o);
    if (OPTS.version == fs_version::adaptive) {
        CHECK(s.splitReports == 2000 / 128);
        CHECK(s.dataShare >= 0.5 - 1e-9 && s.dataShare <= 0.95 + 1e-9);
        CHECK(s.dataShare != 0.9);                  // the first moves are tries, so it has moved
        CHECK(s.cache.ghostHits + s.indexGhostHits > 0);
    } else {
        CHECK(s.splitReports == 0 && s.dataShare == 0.9 && s.indexGhostHits == 0);
    }

    // What the reviews found is kept, and shown to the model at the next open.
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(DB, &db) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    CHECK(sqlite3_prepare_v2(db, "SELECT COUNT(*), MIN(share), MAX(share), MIN(read_time) FROM policy_log", -1, &st, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(st) == SQLITE_ROW);
    if (OPTS.version == fs_version::adaptive) {
        CHECK(sqlite3_column_int(st, 0) == 2000 / 128);
        CHECK(sqlite3_column_double(st, 1) >= 0.5 - 1e-9 && sqlite3_column_double(st, 2) <= 0.95 + 1e-9);
        CHECK(sqlite3_column_double(st, 3) > 0);
    } else {
        CHECK(sqlite3_column_int(st, 0) == 0);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    filesystem again;
    again.open(IMG, DB, OPTS);
    CHECK(again.read("/a", 0, 4096) == bytes(4096, 'a'));
}

static void a_bad_memory_split_from_the_model_is_ignored() {
    if (OPTS.version != fs_version::adaptive) return;
    fs_options o = OPTS;
    o.cacheBytes = 64 * 4096;
    o.modelWaitMs = 0;
    // A stand-in for the model that answers every line with a split no cache could use.
    o.modelCommand = "echo ready; while read line; do echo 'M 7.5'; echo 'M nan'; echo 'M -1'; done";
    const fs_stats s = afterCachePressure(o);
    CHECK(s.splitReports == 2000 / 128);
    CHECK(s.dataShare == 0.9);
}

static void accesses_are_recorded() {
    {
        filesystem fs;
        fresh(fs);
        fs.createFile("/f");
        fs.write("/f", 0, bytes(2 * 4096, 'a'));           // 2 block writes
        fs.read("/f", 0, 3 * 4096);                        // 2 block reads
    }
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(DB, &db) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    CHECK(sqlite3_prepare_v2(db, "SELECT COUNT(*), SUM(is_write) FROM access_log", -1, &st, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(st) == SQLITE_ROW);
    CHECK(sqlite3_column_int(st, 0) == 4);
    CHECK(sqlite3_column_int(st, 1) == 2);
    sqlite3_finalize(st);
    sqlite3_close(db);
}

#define RUN(test)                              \
    do {                                       \
        std::cout << #test << std::endl;       \
        test();                                \
    } while (0)

static void all() {
    RUN(format_creates_empty_root);
    RUN(format_refuses_existing_image);
    RUN(create_file_appears_in_listing);
    RUN(create_rejects_bad_paths);
    RUN(directories_nest);
    RUN(write_then_read_round_trips);
    RUN(write_spans_blocks_at_unaligned_offset);
    RUN(read_is_clamped_to_file_size);
    RUN(hole_reads_as_zeros);
    RUN(overwrite_keeps_surrounding_bytes);
    RUN(files_do_not_share_blocks);
    RUN(remove_frees_blocks_for_reuse);
    RUN(reused_block_does_not_leak_old_data);
    RUN(failed_write_leaves_metadata_unchanged);
    RUN(remove_directory_rules);
    RUN(directories_are_not_files);
    RUN(contents_survive_reopen);
    RUN(open_rejects_missing_database);
    RUN(truncate_shrinks_and_frees_blocks);
    RUN(truncate_then_grow_reads_zeros);
    RUN(rename_moves_and_replaces);
    RUN(rename_rules);
    RUN(contents_match_a_model_under_cache_pressure);
    RUN(sequential_reads_are_prefetched);
    RUN(model_runs_only_in_version_c);
    RUN(predictions_for_missing_blocks_are_ignored);
    RUN(random_reads_are_not_prefetched_by_the_model);
    RUN(model_learns_recorded_history_at_open);
    RUN(a_model_that_cannot_start_is_reported);
    RUN(reads_still_work_when_the_model_dies);
    RUN(memory_split_is_learned_only_in_version_c);
    RUN(a_bad_memory_split_from_the_model_is_ignored);
    RUN(accesses_are_recorded);
}

int main() {
    OPTS.modelWaitMs = 5000;   // wait for the model on every read, so the tests do not depend on timing
    for (fs_version version : {fs_version::baseline, fs_version::predict, fs_version::adaptive}) {
        std::cout << "== version " << "ABC"[int(version)] << std::endl;
        OPTS.version = version;
        all();
    }
    cleanup();
    return 0;
}
