#pragma once
#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

// Map that keeps at most `capacity` entries, dropping the least recently used.
// put() and setCapacity() return what they dropped so the owner can account for it.
template <class K, class V>
class lru {
public:
    using entry = std::pair<K, V>;

    explicit lru(std::size_t capacity = 0) : cap(capacity) {}

    // Null when absent. A found entry becomes the most recently used.
    V* get(const K& key) {
        auto it = map.find(key);
        if (it == map.end()) return nullptr;
        order.splice(order.begin(), order, it->second);
        return &it->second->second;
    }

    bool contains(const K& key) const { return map.count(key) != 0; }

    std::vector<entry> put(const K& key, V value) {
        if (V* old = get(key)) {
            *old = std::move(value);
            return {};
        }
        order.emplace_front(key, std::move(value));
        map[key] = order.begin();
        return trim();
    }

    void erase(const K& key) {
        auto it = map.find(key);
        if (it == map.end()) return;
        order.erase(it->second);
        map.erase(it);
    }

    void clear() {
        order.clear();
        map.clear();
    }

    std::vector<entry> setCapacity(std::size_t capacity) {
        cap = capacity;
        return trim();
    }

    std::size_t size() const { return map.size(); }
    std::size_t capacity() const { return cap; }

private:
    std::vector<entry> trim() {
        std::vector<entry> dropped;
        while (map.size() > cap) {
            map.erase(order.back().first);
            dropped.push_back(std::move(order.back()));
            order.pop_back();
        }
        return dropped;
    }

    std::size_t cap;
    std::list<entry> order;   // most recently used first
    std::unordered_map<K, typename std::list<entry>::iterator> map;
};
