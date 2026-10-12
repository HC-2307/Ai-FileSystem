#include "filesystem.h"
#include "check.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

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

static void cleanup() {
    std::remove(IMG);
    std::remove(DB);
}

// Fresh filesystem of `blocks` blocks, formatted into `fs`.
static void fresh(filesystem& fs, std::uint32_t blocks = 16) {
    cleanup();
    fs.format(IMG, DB, blocks);
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
    fs.open(IMG, DB);
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
    fs.open(IMG, DB);
    CHECK(fs.stat("/d/f").size == 6100);
    CHECK(fs.read("/d/f", 100, 6000) == bytes(6000, 'p'));
}

static void open_rejects_missing_database() {
    { filesystem fs; fresh(fs); }
    std::remove(DB);
    filesystem fs;
    CHECK_THROWS(fs.open(IMG, DB), std::exception);
    CHECK_THROWS(fs.stat("/"), std::logic_error);       // still closed
    FILE* stray = std::fopen(DB, "r");                  // open must not create it
    CHECK(stray == nullptr);
}

#define RUN(test)                              \
    do {                                       \
        std::cout << #test << std::endl;       \
        test();                                \
    } while (0)

int main() {
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
    cleanup();
    return 0;
}
