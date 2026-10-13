#include "block_cache.h"
#include "check.h"
#include "predictor.h"

#include <cstdio>
#include <iostream>
#include <vector>

static const char* IMG = "test_cache.img";

static std::vector<char> block(char c) {
    return std::vector<char>(block_storage::BLOCK_SIZE, c);
}

static void lru_evicts_the_coldest_block() {
    lru<int, int> l(2);
    l.put(1, 10);
    l.put(2, 20);
    CHECK(*l.get(1) == 10);                 // 2 is now the coldest
    auto dropped = l.put(3, 30);
    CHECK(dropped.size() == 1 && dropped[0].first == 2);
    CHECK(!l.contains(2) && l.contains(1) && l.contains(3));
    CHECK(l.setCapacity(1).size() == 1);
    CHECK(l.size() == 1 && l.contains(3));
}

static void cache_counts_hits_misses_and_ghosts() {
    std::remove(IMG);
    block_storage storage;
    storage.create(IMG, 8);
    for (int i = 0; i < 8; i++) storage.writeBlock(i, block(char('a' + i)));
    block_cache cache(storage);
    cache.start(2, 0);
    cache.setGhostCapacity(2);

    CHECK(cache.read(0) == block('a'));     // miss
    CHECK(cache.read(0) == block('a'));     // hit
    cache.read(1);
    cache.read(2);                          // evicts 0
    cache.read(0);                          // miss on a recently evicted block
    cache_stats s = cache.stats();
    CHECK(s.hits == 1 && s.misses == 4 && s.diskReads == 4 && s.ghostHits == 1);

    cache.write(5, block('z'));
    CHECK(cache.read(5) == block('z'));
    CHECK(storage.readBlock(5) == block('z'));   // written through
    CHECK(cache.stats().diskReads == 4);         // served from the cache
}

static void prefetch_is_counted_used_or_wasted() {
    std::remove(IMG);
    block_storage storage;
    storage.create(IMG, 8);
    for (int i = 0; i < 8; i++) storage.writeBlock(i, block(char('a' + i)));
    block_cache cache(storage);
    cache.start(2, 2);

    cache.prefetch(3);
    while (cache.stats().diskReads < 1) std::this_thread::yield();
    CHECK(cache.read(3) == block('d'));
    cache_stats s = cache.stats();
    CHECK(s.prefetchIssued == 1 && s.prefetchUsed == 1 && s.diskReads == 1);

    cache.prefetch(4);
    while (cache.stats().diskReads < 2) std::this_thread::yield();
    cache.read(4);
    cache.prefetch(5);
    while (cache.stats().diskReads < 3) std::this_thread::yield();
    cache.read(6);
    cache.read(7);                          // 5 leaves the cache unread
    s = cache.stats();
    CHECK(s.prefetchUsed == 2 && s.prefetchWasted == 1);
}

static void prefetch_never_brings_back_old_data() {
    std::remove(IMG);
    block_storage storage;
    storage.create(IMG, 4);
    block_cache cache(storage);
    cache.start(4, 4);
    for (int i = 0; i < 2000; i++) {
        const std::uint32_t b = i % 4;
        cache.invalidate(b);
        cache.prefetch(b);
        cache.write(b, block(char(i)));     // races the prefetch of the old contents
        CHECK(cache.read(b) == block(char(i)));
    }
    std::remove(IMG);
}

static void predictor_follows_a_sequential_scan() {
    predictor p;
    p.observe(1, 0, 2);
    p.observe(1, 1, 2);
    std::vector<prediction> next = p.observe(1, 2, 2);
    CHECK(next.size() == 2);
    CHECK(next[0].inode == 1 && next[0].idx == 3 && next[1].idx == 4);
    CHECK(next[0].confidence >= 0.5);
    for (int i = 3; i < 10; i++) next = p.observe(1, i, 1);
    CHECK(next[0].idx == 10 && next[0].confidence > 0.85);
}

static void predictor_learns_a_repeating_order() {
    const int order[] = {40, 7, 23, 91, 15};
    predictor p;
    for (int round = 0; round < 3; round++)
        for (int idx : order) p.observe(1, idx, 1);
    std::vector<prediction> next = p.observe(1, 40, 2);
    CHECK(next.size() == 2);
    CHECK(next[0].idx == 7 && next[1].idx == 23);
    CHECK(next[0].confidence >= 0.5);
}

#define RUN(test)                              \
    do {                                       \
        std::cout << #test << std::endl;       \
        test();                                \
    } while (0)

int main() {
    RUN(lru_evicts_the_coldest_block);
    RUN(cache_counts_hits_misses_and_ghosts);
    RUN(prefetch_is_counted_used_or_wasted);
    RUN(prefetch_never_brings_back_old_data);
    RUN(predictor_follows_a_sequential_scan);
    RUN(predictor_learns_a_repeating_order);
    return 0;
}
