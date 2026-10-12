#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// The knobs the adaptive version turns, and the rules for turning them from
// what was observed. Versions A and B never call learn() or rebalance(), so
// they keep the defaults.
struct policy {
    double threshold = 0.5;   // minimum confidence before a block is prefetched
    int depth = 2;            // how many blocks ahead to prefetch
    double dataShare = 0.9;   // part of the memory budget given to block data; the rest caches block mappings

    // Feedback on prefetching: `used` were read after being prefetched,
    // `wasted` were evicted unread. Mostly used means prefetch more boldly;
    // mostly wasted means back off.
    void learn(std::uint64_t used, std::uint64_t wasted) {
        usedSoFar += used;
        wastedSoFar += wasted;
        const std::uint64_t outcomes = usedSoFar + wastedSoFar;
        if (outcomes < 16) return;
        const double useful = double(usedSoFar) / outcomes;
        if (useful >= 0.8) {
            depth = std::min(8, depth + 1);
            threshold = std::max(0.3, threshold - 0.05);
        } else if (useful < 0.5) {
            depth = std::max(1, depth - 1);
            threshold = std::min(0.9, threshold + 0.05);
        }
        usedSoFar = wastedSoFar = 0;
    }

    // Moves one STEP of the memory budget toward the cache that would gain
    // more time from it. Each ghost list is sized to hold what one more STEP
    // would have held, so a ghost hit is a miss that the step would have
    // avoided; the costs are what one miss takes in each cache. The index
    // sizes are in entries, `indexStep` being how many one STEP holds.
    static constexpr double STEP = 0.05;
    void rebalance(std::uint64_t dataGhostHits, std::uint64_t indexGhostHits, double dataMissCost,
                   double indexMissCost, std::size_t indexUsed, std::size_t indexCapacity, std::size_t indexStep) {
        const double data = dataGhostHits * dataMissCost;
        const double index = indexGhostHits * indexMissCost;
        const bool full = indexUsed >= indexCapacity;
        // Room the index has left unused and is not about to refill costs nothing to take.
        const bool idle = indexUsed + indexStep <= indexCapacity && indexGhostHits == 0;
        if (index > data) {
            if (full) dataShare = std::max(STEP, dataShare - STEP);   // more room is no use to an index with space left
        } else if (data > index || (idle && dataGhostHits > 0)) {
            dataShare = std::min(1 - STEP, dataShare + STEP);
        }
    }

private:
    std::uint64_t usedSoFar = 0, wastedSoFar = 0;
};
