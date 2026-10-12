#pragma once
#include "block_storage.h"
#include "lru.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct cache_stats {
    std::uint64_t hits = 0, misses = 0;
    std::uint64_t ghostHits = 0;        // misses on a block that was evicted recently
    std::uint64_t diskReads = 0, diskWrites = 0, diskReadNanos = 0;
    std::uint64_t prefetchIssued = 0;
    std::uint64_t prefetchUsed = 0;     // prefetched blocks that were then read
    std::uint64_t prefetchWasted = 0;   // prefetched blocks evicted without being read
};

// Write-through LRU cache of whole blocks in front of block_storage, with
// background threads that load blocks ahead of time. read(), write(),
// invalidate(), prefetch() and setCapacity() must all come from one thread;
// only the prefetch workers run beside it.
class block_cache {
public:
    explicit block_cache(block_storage& storage) : storage(storage) {}
    ~block_cache();
    block_cache(const block_cache&) = delete;
    block_cache& operator=(const block_cache&) = delete;

    // Empties the cache, zeroes the statistics and starts the workers.
    void start(std::size_t capacityBlocks, unsigned prefetchThreads);
    // How many recently evicted blocks to remember for cache_stats::ghostHits.
    void setGhostCapacity(std::size_t blocks);
    void stop();

    std::vector<char> read(std::uint32_t block);
    void write(std::uint32_t block, const std::vector<char>& data);   // data is one full block
    // Forget a block whose contents no longer belong to anyone.
    void invalidate(std::uint32_t block);
    // Ask for a block to be loaded in the background. A block already cached
    // is refreshed instead, so it is not evicted just before it is needed.
    void prefetch(std::uint32_t block);

    void setCapacity(std::size_t blocks);
    std::size_t capacity();
    std::size_t size();
    cache_stats stats();

private:
    struct slot {
        std::vector<char> data;
        bool prefetched;   // loaded ahead of time and not read yet
    };
    enum class flight { queued, reading, stale };

    void insert(std::uint32_t block, slot s);
    void dropped(const std::vector<lru<std::uint32_t, slot>::entry>& entries);
    void cancel(std::uint32_t block);
    void work();

    block_storage& storage;
    std::mutex m;
    std::condition_variable wake, landed;
    lru<std::uint32_t, slot> blocks;
    lru<std::uint32_t, char> ghost;   // keys of recently evicted blocks
    std::unordered_map<std::uint32_t, flight> inflight;
    std::deque<std::uint32_t> queue;
    std::vector<std::thread> workers;
    bool stopping = false;
    cache_stats counters;
};
