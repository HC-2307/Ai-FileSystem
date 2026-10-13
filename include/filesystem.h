#pragma once
#include "block_cache.h"
#include "block_storage.h"
#include "lru.h"
#include "model_client.h"
#include "predictor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

struct file_info {
    bool isDir;
    std::uint64_t size;
};

// The three versions the experiments compare. A and B are the comparisons;
// C is the project.
enum class fs_version {
    baseline,   // A: LRU caches, no prefetching
    predict,    // B: A + prefetching from two hand-written detectors (predictor.h)
    adaptive,   // C: A + prefetching from a machine-learning model trained on the reads (python/model.py)
};

struct fs_options {
    fs_version version = fs_version::baseline;
    std::size_t cacheBytes = 4 << 20;   // memory shared by the block cache and the mapping cache
    io_mode mode = io_mode::buffered;
    std::string modelCommand;           // version C: how to start the model; empty means python/model.py
    int modelWaitMs = 0;                // version C: how long a read waits for the model to take it in; 0 means not at all
};

struct fs_stats {
    cache_stats cache;
    std::uint64_t indexHits = 0, indexMisses = 0;
    std::uint64_t dbLookups = 0, dbLookupNanos = 0;   // mapping queries that reached SQLite
    std::uint64_t memoryBytes = 0;                    // held by both caches right now
    double threshold = 0, dataShare = 0;              // version B's fixed settings and the cache split
    int depth = 0;
    std::uint64_t modelUpdates = 0;                   // version C: reads the model has been shown
    double modelAccuracy = 0;                         // version C: running share of reads that were its first guess
};

// Files and directories on top of block_storage. SQLite holds the metadata
// (names, sizes, which blocks belong to which file, which blocks are free);
// the image file holds the data. Paths are absolute and '/'-separated.
// Failures throw std::system_error carrying an errno value (ENOENT, EEXIST,
// ENOSPC, ...), so a FUSE adapter can return it directly.
// Caching and prediction only change speed: every version returns the same
// bytes. Not thread-safe; call from one thread.
class filesystem {
public:
    filesystem() = default;
    ~filesystem();
    filesystem(const filesystem&) = delete;
    filesystem& operator=(const filesystem&) = delete;

    void format(const std::string& imagePath, const std::string& dbPath, std::uint32_t blockCount,
                const fs_options& options = {});
    void open(const std::string& imagePath, const std::string& dbPath, const fs_options& options = {});
    void close();

    void createFile(const std::string& path);
    void mkdir(const std::string& path);
    void remove(const std::string& path);   // file or empty directory
    // Replaces `to` if it exists and is a file or an empty directory of the same kind.
    void rename(const std::string& from, const std::string& to);
    // Shrinking frees blocks; growing leaves a hole that reads as zeros.
    void truncate(const std::string& path, std::uint64_t size);
    std::vector<std::string> list(const std::string& path);
    file_info stat(const std::string& path);

    void write(const std::string& path, std::uint64_t offset, const std::vector<char>& data);
    // Returns fewer than `size` bytes when the range runs past the end of the file.
    std::vector<char> read(const std::string& path, std::uint64_t offset, std::size_t size);

    fs_stats stats();

private:
    struct access {
        std::int64_t inode;
        std::uint64_t idx;
        bool isWrite;
    };

    void start(const fs_options& options);
    void create(const std::string& path, bool isDir);
    void unlink(std::int64_t id, const std::string& path);
    std::int64_t blockOf(std::int64_t inode, std::uint64_t idx, bool readAhead = false);
    void remember(std::uint64_t key, std::int64_t block);
    void forgetMappings();
    void anticipate(std::int64_t inode, std::uint64_t idx);
    void resize();
    void record(std::int64_t inode, std::uint64_t idx, bool isWrite);
    void flushLog();

    block_storage storage;
    block_cache cache{storage};
    sqlite3* db = nullptr;
    fs_options options;

    // Which block holds (file, index), so most lookups skip SQLite. -1 is a hole.
    lru<std::uint64_t, std::int64_t> index;
    predictor guesser;                   // version B only
    model_client model;                  // version C only
    fs_stats counters;
    std::vector<access> log;             // not yet written to access_log
};
