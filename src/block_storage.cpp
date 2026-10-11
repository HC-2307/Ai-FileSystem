#include "block_storage.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static std::system_error sysError(const std::string& what) {
    return std::system_error(errno, std::generic_category(), what);
}

// Transfer exactly one block. pread/pwrite may move fewer bytes than asked,
// so loop until the block is complete. `direct` is unused until direct I/O lands.
static void readFully(int fd, char* buf, off_t offset, bool direct) {
    (void)direct;
    std::size_t done = 0;
    while (done < block_storage::BLOCK_SIZE) {
        ssize_t n = ::pread(fd, buf + done, block_storage::BLOCK_SIZE - done, offset + off_t(done));
        if (n == -1) {
            if (errno == EINTR) continue;
            throw sysError("pread");
        }
        if (n == 0) {
            errno = EIO;
            throw sysError("pread: image shorter than expected");
        }
        done += std::size_t(n);
    }
}

static void writeFully(int fd, const char* buf, off_t offset, bool direct) {
    (void)direct;
    std::size_t done = 0;
    while (done < block_storage::BLOCK_SIZE) {
        ssize_t n = ::pwrite(fd, buf + done, block_storage::BLOCK_SIZE - done, offset + off_t(done));
        if (n == -1) {
            if (errno == EINTR) continue;
            throw sysError("pwrite");
        }
        done += std::size_t(n);
    }
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

off_t block_storage::checkedOffset(std::uint32_t blockNumber) const {
    if (!isOpen()) throw std::logic_error("block_storage not open");
    if (blockNumber >= blockCount) throw std::out_of_range("block number out of range");
    return off_t(blockNumber) * off_t(BLOCK_SIZE);
}

void block_storage::writeBlock(std::uint32_t blockNumber, const std::vector<char>& data) {
    off_t offset = checkedOffset(blockNumber);
    if (data.size() > BLOCK_SIZE) throw std::invalid_argument("data larger than a block");

    alignas(BLOCK_SIZE) char buf[BLOCK_SIZE] = {};
    if (!data.empty()) std::memcpy(buf, data.data(), data.size());
    writeFully(fd, buf, offset, false);
}

std::vector<char> block_storage::readBlock(std::uint32_t blockNumber) {
    off_t offset = checkedOffset(blockNumber);

    alignas(BLOCK_SIZE) char buf[BLOCK_SIZE];
    readFully(fd, buf, offset, false);
    return std::vector<char>(buf, buf + BLOCK_SIZE);
}
