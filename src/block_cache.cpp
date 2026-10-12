#include "block_cache.h"

#include <chrono>

namespace {

const std::size_t MAX_QUEUED = 64;

std::uint64_t nanosSince(std::chrono::steady_clock::time_point start) {
    return std::uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

}  // namespace

block_cache::~block_cache() {
    stop();
}

void block_cache::start(std::size_t capacityBlocks, unsigned prefetchThreads) {
    stop();
    blocks.setCapacity(capacityBlocks);
    ghost.setCapacity(capacityBlocks);
    counters = cache_stats{};
    stopping = false;
    for (unsigned i = 0; i < prefetchThreads; i++) workers.emplace_back([this] { work(); });
}

void block_cache::stop() {
    {
        std::lock_guard<std::mutex> lock(m);
        stopping = true;
    }
    wake.notify_all();
    for (std::thread& t : workers) t.join();
    workers.clear();
    blocks.clear();
    ghost.clear();
    inflight.clear();
    queue.clear();
}

std::vector<char> block_cache::read(std::uint32_t block) {
    std::unique_lock<std::mutex> lock(m);
    auto flying = inflight.find(block);
    if (flying != inflight.end()) {
        // Queued but not started: reading it here is faster than waiting in line.
        if (flying->second == flight::queued) inflight.erase(flying);
        else landed.wait(lock, [&] { return inflight.count(block) == 0; });
    }
    if (slot* s = blocks.get(block)) {
        counters.hits++;
        if (s->prefetched) {
            counters.prefetchUsed++;
            s->prefetched = false;
        }
        return s->data;
    }
    counters.misses++;
    if (ghost.contains(block)) counters.ghostHits++;

    lock.unlock();
    auto start = std::chrono::steady_clock::now();
    std::vector<char> data = storage.readBlock(block);
    std::uint64_t nanos = nanosSince(start);
    lock.lock();
    counters.diskReads++;
    counters.diskReadNanos += nanos;
    insert(block, slot{data, false});
    return data;
}

void block_cache::write(std::uint32_t block, const std::vector<char>& data) {
    {
        std::lock_guard<std::mutex> lock(m);
        cancel(block);
        insert(block, slot{data, false});
        counters.diskWrites++;
    }
    storage.writeBlock(block, data);
}

void block_cache::invalidate(std::uint32_t block) {
    std::lock_guard<std::mutex> lock(m);
    cancel(block);
    blocks.erase(block);
    ghost.erase(block);
}

void block_cache::prefetch(std::uint32_t block) {
    std::lock_guard<std::mutex> lock(m);
    if (blocks.get(block) || inflight.count(block) || queue.size() >= MAX_QUEUED || workers.empty()) return;
    inflight[block] = flight::queued;
    queue.push_back(block);
    counters.prefetchIssued++;
    wake.notify_one();
}

void block_cache::setCapacity(std::size_t n) {
    std::lock_guard<std::mutex> lock(m);
    dropped(blocks.setCapacity(n));
}

void block_cache::setGhostCapacity(std::size_t n) {
    std::lock_guard<std::mutex> lock(m);
    ghost.setCapacity(n);
}

std::size_t block_cache::capacity() {
    std::lock_guard<std::mutex> lock(m);
    return blocks.capacity();
}

std::size_t block_cache::size() {
    std::lock_guard<std::mutex> lock(m);
    return blocks.size();
}

cache_stats block_cache::stats() {
    std::lock_guard<std::mutex> lock(m);
    return counters;
}

// Callers hold the mutex for everything below except work().

void block_cache::insert(std::uint32_t block, slot s) {
    ghost.erase(block);
    dropped(blocks.put(block, std::move(s)));
}

void block_cache::dropped(const std::vector<lru<std::uint32_t, slot>::entry>& entries) {
    for (const auto& e : entries) {
        if (e.second.prefetched) counters.prefetchWasted++;
        ghost.put(e.first, 0);
    }
}

// A prefetch of this block that is under way would bring back old contents.
void block_cache::cancel(std::uint32_t block) {
    auto it = inflight.find(block);
    if (it == inflight.end()) return;
    if (it->second == flight::queued) inflight.erase(it);
    else it->second = flight::stale;
}

void block_cache::work() {
    std::unique_lock<std::mutex> lock(m);
    for (;;) {
        wake.wait(lock, [&] { return stopping || !queue.empty(); });
        if (stopping) return;
        std::uint32_t block = queue.front();
        queue.pop_front();
        auto it = inflight.find(block);
        if (it == inflight.end() || it->second != flight::queued) continue;   // cancelled
        it->second = flight::reading;

        lock.unlock();
        std::vector<char> data;
        bool ok = true;
        auto start = std::chrono::steady_clock::now();
        try {
            data = storage.readBlock(block);
        } catch (...) {
            ok = false;   // the reader that needs this block will hit the error itself
        }
        std::uint64_t nanos = nanosSince(start);
        lock.lock();

        bool stale = inflight[block] == flight::stale;
        inflight.erase(block);
        if (ok) {
            counters.diskReads++;
            counters.diskReadNanos += nanos;
            if (!stale) insert(block, slot{std::move(data), true});
        }
        landed.notify_all();
    }
}
