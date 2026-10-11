#include "block_storage.h"
#include "check.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <vector>

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
    std::remove("test_zero.img");
    block_storage s;
    CHECK_THROWS(s.create("test_zero.img", 0), std::invalid_argument);
    CHECK(!std::filesystem::exists("test_zero.img"));
}

static void open_rejects_malformed_image() {
    block_storage s;
    std::remove("test_missing.img");
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

#define RUN(test)                              \
    do {                                       \
        std::cout << #test << std::endl;       \
        test();                                \
    } while (0)

int main() {
    RUN(create_makes_image_of_exact_size);
    RUN(open_reads_block_count_from_file_size);
    RUN(create_refuses_existing_file);
    RUN(create_rejects_zero_blocks);
    RUN(open_rejects_malformed_image);
    RUN(second_open_on_open_storage_throws);
    RUN(write_then_read_round_trips);
    RUN(unwritten_block_reads_as_zeros);
    RUN(short_write_is_zero_padded);
    RUN(oversized_write_throws);
    RUN(writes_do_not_touch_neighbours);
    RUN(data_survives_reopen);
    RUN(out_of_range_block_throws);
    RUN(io_on_closed_storage_throws);
    RUN(last_block_of_large_image);
    RUN(direct_mode_round_trips);
    RUN(direct_and_buffered_see_the_same_data);
    RUN(mode_defaults_to_buffered);
    return 0;
}
