#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

// One block of one file. Block indexes fit in 32 bits because an image holds
// at most 2^32 blocks.
inline std::uint64_t blockKey(std::int64_t inode, std::uint64_t idx) {
    return (std::uint64_t(inode) << 32) | idx;
}

struct prediction {
    std::int64_t inode;
    std::uint64_t idx;
    double confidence;   // 0 to 1
};

// Version B's predictor: hand-written, no machine learning. Guesses which
// blocks will be read next from the reads seen so far. Two detectors vote: a stride detector per file (catches sequential and fixed-step
// scans the first time they happen) and a table of which block followed which
// (catches any order that repeats).
class predictor {
public:
    // Records a read and returns up to `depth` blocks per model that are
    // likely to follow it, without duplicates.
    std::vector<prediction> observe(std::int64_t inode, std::uint64_t idx, int depth);

    double strideAccuracy() const { return accuracy[0]; }
    double tableAccuracy() const { return accuracy[1]; }

private:
    struct stream {
        std::uint64_t last;
        std::int64_t delta;
        int streak;   // consecutive reads that repeated `delta`
    };
    struct followers {
        std::unordered_map<std::uint64_t, std::uint32_t> count;
        std::uint32_t total = 0;
    };

    void score(int model, bool right);
    const std::pair<const std::uint64_t, std::uint32_t>* likeliest(std::uint64_t key, double& share) const;

    std::unordered_map<std::int64_t, stream> streams;
    std::unordered_map<std::uint64_t, followers> table;
    bool hasLast = false;
    std::uint64_t lastKey = 0;
    bool guessed = false;   // the table named a follower for lastKey
    std::uint64_t guess = 0;
    double accuracy[2] = {0.5, 0.5};
};
