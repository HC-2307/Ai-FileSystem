#include "block_storage.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static std::system_error sysError(const std::string& what) {
    return std::system_error(errno, std::generic_category(), what);
}

block_storage::~block_storage() {
    close();
}

void block_storage::create(const std::string& filename, std::uint32_t blockCount) {
    if (isOpen()) throw std::logic_error("block_storage already open");
    if (blockCount == 0) throw std::invalid_argument("blockCount must be > 0");

    int newFd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    if (newFd == -1) throw sysError("create " + filename);

    // Sparse: unwritten blocks read back as zeros.
    if (::ftruncate(newFd, off_t(blockCount) * off_t(BLOCK_SIZE)) == -1) {
        std::system_error err = sysError("ftruncate " + filename);
        ::close(newFd);
        ::unlink(filename.c_str());
        throw err;
    }

    fd = newFd;
    this->blockCount = blockCount;
}

void block_storage::open(const std::string& filename) {
    if (isOpen()) throw std::logic_error("block_storage already open");

    int newFd = ::open(filename.c_str(), O_RDWR);
    if (newFd == -1) throw sysError("open " + filename);

    struct stat st;
    if (::fstat(newFd, &st) == -1) {
        std::system_error err = sysError("fstat " + filename);
        ::close(newFd);
        throw err;
    }
    if (st.st_size == 0 || st.st_size % off_t(BLOCK_SIZE) != 0) {
        ::close(newFd);
        throw std::invalid_argument(filename + " is not a block image");
    }

    fd = newFd;
    blockCount = std::uint32_t(st.st_size / off_t(BLOCK_SIZE));
}

void block_storage::close() {
    if (fd == -1) return;
    ::close(fd);
    fd = -1;
    blockCount = 0;
}

bool block_storage::isOpen() const {
    return fd != -1;
}

std::uint32_t block_storage::getBlockCount() const {
    return blockCount;
}
