#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class block_storage {
public:
    static constexpr std::size_t BLOCK_SIZE = 4096;

    block_storage() = default;
    ~block_storage();
    block_storage(const block_storage&) = delete;
    block_storage& operator=(const block_storage&) = delete;

    void create(const std::string& filename, std::uint32_t blockCount);
    void open(const std::string& filename);
    void close();
    bool isOpen() const;
    std::uint32_t getBlockCount() const;

private:
    int fd = -1;
    std::uint32_t blockCount = 0;
};
