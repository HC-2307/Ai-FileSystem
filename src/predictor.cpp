#include "predictor.h"

#include <algorithm>

namespace {

enum { STRIDE = 0, TABLE = 1 };
const std::size_t MAX_TABLE_KEYS = 1 << 18;

void add(std::vector<prediction>& out, std::uint64_t key, double confidence) {
    prediction p{std::int64_t(key >> 32), key & 0xffffffffu, confidence};
    for (prediction& have : out) {
        if (have.inode == p.inode && have.idx == p.idx) {
            have.confidence = std::max(have.confidence, confidence);
            return;
        }
    }
    out.push_back(p);
}

}  // namespace

void predictor::score(int model, bool right) {
    accuracy[model] = 0.9 * accuracy[model] + 0.1 * (right ? 1.0 : 0.0);
}

const std::pair<const std::uint64_t, std::uint32_t>* predictor::likeliest(std::uint64_t key, double& share) const {
    auto it = table.find(key);
    if (it == table.end()) return nullptr;
    const std::pair<const std::uint64_t, std::uint32_t>* best = nullptr;
    for (const auto& f : it->second.count)
        if (!best || f.second > best->second) best = &f;
    // +1 so a follower seen once is a coin flip, not a certainty.
    share = double(best->second) / (it->second.total + 1);
    return best;
}

std::vector<prediction> predictor::observe(std::int64_t inode, std::uint64_t idx, int depth) {
    const std::uint64_t key = blockKey(inode, idx);
    std::vector<prediction> out;

    auto found = streams.try_emplace(inode, stream{idx, 0, 0});
    stream& s = found.first->second;
    if (!found.second) {
        const std::int64_t delta = std::int64_t(idx) - std::int64_t(s.last);
        if (s.delta != 0) score(STRIDE, delta == s.delta);
        s.streak = (delta != 0 && delta == s.delta) ? s.streak + 1 : 0;
        s.delta = delta;
        s.last = idx;
    }
    if (s.streak > 0) {
        const double confidence = s.streak / (s.streak + 1.0);
        for (int k = 1; k <= depth; k++) {
            const std::int64_t next = std::int64_t(idx) + s.delta * k;
            if (next < 0 || next > 0xffffffffLL) break;
            add(out, blockKey(inode, std::uint64_t(next)), confidence);
        }
    }

    if (hasLast) {
        if (guessed) score(TABLE, guess == key);
        // shortcut: the table is dropped whole when full, relearn cost is one
        // pass; switch to evicting cold keys if images grow past ~1 GB.
        if (table.size() >= MAX_TABLE_KEYS) table.clear();
        followers& f = table[lastKey];
        f.count[key]++;
        f.total++;
    }
    hasLast = true;
    lastKey = key;

    double confidence = 1, share = 0;
    const auto* first = likeliest(key, share);
    guessed = first != nullptr;
    if (first) guess = first->first;
    std::uint64_t at = key;
    for (int k = 0; k < depth; k++) {
        const auto* next = likeliest(at, share);
        if (!next) break;
        confidence *= share;
        add(out, next->first, confidence);
        at = next->first;
    }
    return out;
}
