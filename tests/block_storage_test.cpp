#include "block_storage.h"
#include "check.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

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
    return 0;
}
