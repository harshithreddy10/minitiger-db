#ifndef MINITIGER_STORAGE_LRU_CACHE_HPP
#define MINITIGER_STORAGE_LRU_CACHE_HPP

#include "document.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace minitiger {

/**
 * @brief O(1) Least Recently Used (LRU) Document Page Cache.
 *
 * Combines a doubly linked list (std::list) tracking recency order with a
 * hash table (std::unordered_map) mapping document IDs to list iterators.
 * Inspired by the WiredTiger in-memory cache eviction architecture.
 */
class LRUCache {
public:
    using Entry = std::pair<std::string, Document>;
    using ListIter = std::list<Entry>::iterator;

    explicit LRUCache(size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("LRUCache capacity must be greater than 0");
        }
    }

    /**
     * @brief Looks up a document by ID and promotes it to Most Recently Used (MRU)
     * in O(1) time via std::list::splice.
     */
    std::optional<Document> get(const std::string& doc_id) {
        auto it = index_.find(doc_id);
        if (it == index_.end()) {
            ++misses_;
            return std::nullopt;
        }
        // Move accessed node to the front (MRU position) in O(1)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        ++hits_;
        return it->second->second;
    }

    /**
     * @brief Inserts or updates a document in O(1) time.
     * If capacity is exceeded, evicts the Least Recently Used (LRU) document from the back
     * and returns the evicted Document.
     */
    std::optional<Document> put(const std::string& doc_id, const Document& doc) {
        auto it = index_.find(doc_id);
        if (it != index_.end()) {
            it->second->second = doc;
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            return std::nullopt;
        }

        std::optional<Document> evicted = std::nullopt;
        if (lru_list_.size() >= capacity_) {
            const auto& lru_entry = lru_list_.back();
            evicted = lru_entry.second;
            index_.erase(lru_entry.first);
            lru_list_.pop_back();
            ++evictions_;
        }

        lru_list_.emplace_front(doc_id, doc);
        index_[doc_id] = lru_list_.begin();
        return evicted;
    }

    /**
     * @brief Removes a document from the cache in O(1) time if present.
     */
    bool erase(const std::string& doc_id) {
        auto it = index_.find(doc_id);
        if (it == index_.end()) {
            return false;
        }
        lru_list_.erase(it->second);
        index_.erase(it);
        return true;
    }

    size_t size() const noexcept { return lru_list_.size(); }
    size_t capacity() const noexcept { return capacity_; }
    uint64_t hits() const noexcept { return hits_; }
    uint64_t misses() const noexcept { return misses_; }
    uint64_t evictions() const noexcept { return evictions_; }

    double hit_ratio() const noexcept {
        uint64_t total = hits_ + misses_;
        return total == 0 ? 0.0 : static_cast<double>(hits_) / static_cast<double>(total);
    }

private:
    size_t                                     capacity_;
    std::list<Entry>                           lru_list_;
    std::unordered_map<std::string, ListIter>  index_;
    uint64_t                                   hits_{0};
    uint64_t                                   misses_{0};
    uint64_t                                   evictions_{0};
};

} // namespace minitiger

#endif // MINITIGER_STORAGE_LRU_CACHE_HPP