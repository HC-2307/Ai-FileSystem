#pragma once
#include "block_storage.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

struct file_info {
    bool isDir;
    std::uint64_t size;
};

// Files and directories on top of block_storage. SQLite holds the metadata
// (names, sizes, which blocks belong to which file, which blocks are free);
// the image file holds the data. Paths are absolute and '/'-separated.
// Failures throw std::system_error carrying an errno value (ENOENT, EEXIST,
// ENOSPC, ...), so a FUSE adapter can return it directly.
class filesystem {
public:
    filesystem() = default;
    ~filesystem();
    filesystem(const filesystem&) = delete;
    filesystem& operator=(const filesystem&) = delete;

    void format(const std::string& imagePath, const std::string& dbPath, std::uint32_t blockCount,
                io_mode mode = io_mode::buffered);
    void open(const std::string& imagePath, const std::string& dbPath, io_mode mode = io_mode::buffered);
    void close();

    void createFile(const std::string& path);
    void mkdir(const std::string& path);
    void remove(const std::string& path);   // file or empty directory
    std::vector<std::string> list(const std::string& path);
    file_info stat(const std::string& path);

    void write(const std::string& path, std::uint64_t offset, const std::vector<char>& data);
    // Returns fewer than `size` bytes when the range runs past the end of the file.
    std::vector<char> read(const std::string& path, std::uint64_t offset, std::size_t size);

private:
    void create(const std::string& path, bool isDir);

    block_storage storage;
    sqlite3* db = nullptr;
};
