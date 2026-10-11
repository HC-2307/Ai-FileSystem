#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <sys/types.h>

// direct bypasses the kernel page cache (O_DIRECT), so every read and write
// reaches the image file. Not supported on every filesystem (e.g. tmpfs).
enum class io_mode { buffered, direct };

class block_storage {
public:
    static constexpr std::size_t BLOCK_SIZE = 4096;

    block_storage() = default;
    ~block_storage();
    block_storage(const block_storage&) = delete;
    block_storage& operator=(const block_storage&) = delete;

    void create(const std::string& filename, std::uint32_t blockCount, io_mode mode = io_mode::buffered);
    void open(const std::string& filename, io_mode mode = io_mode::buffered);
    void close();
    bool isOpen() const;
    std::uint32_t getBlockCount() const;
    io_mode getMode() const;

    // Data shorter than BLOCK_SIZE is zero-padded; longer data is rejected.
    void writeBlock(std::uint32_t blockNumber, const std::vector<char>& data);
    std::vector<char> readBlock(std::uint32_t blockNumber);

private:
    off_t checkedOffset(std::uint32_t blockNumber) const;

    int fd = -1;
    std::uint32_t blockCount = 0;
    io_mode mode = io_mode::buffered;
};
