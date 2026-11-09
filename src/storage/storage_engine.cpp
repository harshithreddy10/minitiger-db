#include "storage_engine.hpp"

#include <algorithm>
#include <functional>

namespace minitiger {

StorageEngine::StorageEngine(const std::string& wal_path,
                             size_t total_cache_capacity,
                             bool sync_wal_on_write)
    : wal_(wal_path, sync_wal_on_write) {
    size_t per_stripe_capacity = std::max<size_t>(1, total_cache_capacity / kNumStripes);
    for (auto& stripe : stripes_) {
        stripe.cache = std::make_unique<LRUCache>(per_stripe_capacity);
    }

    // Automatically recover any committed state from the Write-Ahead Log on startup
    auto recovered_records = WriteAheadLog::recover(wal_path);
    auto recovered_map = WriteAheadLog::replay_to_map(recovered_records);
    for (const auto& [doc_id, doc] : recovered_map) {
        auto& stripe = stripes_[stripe_index(doc_id)];
        stripe.disk_pages[doc_id] = doc.serialize();
        stripe.cache->put(doc_id, doc);
    }
}

size_t StorageEngine::stripe_index(const std::string& doc_id) const noexcept {
    return std::hash<std::string>{}(doc_id) % kNumStripes;
}

uint64_t StorageEngine::put(const Document& doc) {
    const std::string& id = doc.id();
    auto& stripe = stripes_[stripe_index(id)];

    // Acquire exclusive lock ONLY for this document's 1/64th stripe
    std::unique_lock<std::shared_mutex> lock(stripe.mutex);

    bool is_update = (stripe.disk_pages.find(id) != stripe.disk_pages.end());
    uint64_t lsn = is_update ? wal_.append_update(doc) : wal_.append_insert(doc);

    stripe.disk_pages[id] = doc.serialize();
    stripe.cache->put(id, doc);
    return lsn;
}

std::optional<Document> StorageEngine::get(const std::string& doc_id) {
    auto& stripe = stripes_[stripe_index(doc_id)];

    // Acquire exclusive lock on this stripe because LRUCache::get() mutates the recency list
    std::unique_lock<std::shared_mutex> lock(stripe.mutex);

    // 1. Fast path: O(1) in-memory LRU cache hit
    if (auto cached = stripe.cache->get(doc_id); cached.has_value()) {
        return cached;
    }

    // 2. Slow path: Cache miss -> deserialize from binary backing page and promote into LRU cache
    auto page_it = stripe.disk_pages.find(doc_id);
    if (page_it == stripe.disk_pages.end()) {
        return std::nullopt;
    }

    Document decoded = Document::deserialize(page_it->second);
    stripe.cache->put(doc_id, decoded);
    return decoded;
}

bool StorageEngine::remove(const std::string& doc_id) {
    auto& stripe = stripes_[stripe_index(doc_id)];
    std::unique_lock<std::shared_mutex> lock(stripe.mutex);

    auto page_it = stripe.disk_pages.find(doc_id);
    if (page_it == stripe.disk_pages.end()) {
        return false;
    }

    wal_.append_delete(doc_id);
    stripe.disk_pages.erase(page_it);
    stripe.cache->erase(doc_id);
    return true;
}

uint64_t StorageEngine::checkpoint() {
    // Acquire shared locks across all stripes in deterministic order to flush a consistent state
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(kNumStripes);
    for (const auto& stripe : stripes_) {
        locks.emplace_back(stripe.mutex);
    }
    uint64_t lsn = wal_.append_checkpoint();
    wal_.flush();
    return lsn;
}

std::vector<Document> StorageEngine::scan_all() const {
    std::vector<Document> results;
    for (const auto& stripe : stripes_) {
        std::shared_lock<std::shared_mutex> lock(stripe.mutex);
        for (const auto& [doc_id, raw_bytes] : stripe.disk_pages) {
            results.push_back(Document::deserialize(raw_bytes));
        }
    }
    return results;
}

StorageStats StorageEngine::stats() const {
    StorageStats st;
    for (const auto& stripe : stripes_) {
        std::shared_lock<std::shared_mutex> lock(stripe.mutex);
        st.total_documents += stripe.disk_pages.size();
        st.cached_documents += stripe.cache->size();
        st.cache_hits += stripe.cache->hits();
        st.cache_misses += stripe.cache->misses();
        st.cache_evictions += stripe.cache->evictions();
    }
    uint64_t total_lookups = st.cache_hits + st.cache_misses;
    st.cache_hit_ratio = total_lookups == 0
                             ? 0.0
                             : static_cast<double>(st.cache_hits) / static_cast<double>(total_lookups);
    st.wal_lsn = wal_.current_lsn();
    return st;
}

} // namespace minitiger