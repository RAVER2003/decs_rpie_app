#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <list>
#include <unordered_map>
#include <string>

struct LRUCacheShard {
    std::mutex mtx;
    size_t capacity_bytes;
    size_t current_bytes;

    std::list<std::pair<std::string,std::string>> lru;
    std::unordered_map<std::string, decltype(lru.begin())> map;

    LRUCacheShard(size_t cap = 0)
        : capacity_bytes(cap), current_bytes(0) {}
};

class LRUCache {
public:
    int shard_count;
    size_t per_shard_bytes;

    std::vector<std::unique_ptr<LRUCacheShard>> shards;

    LRUCache(int shard_cnt, size_t total_bytes)
        : shard_count(shard_cnt)
    {
        per_shard_bytes = total_bytes / shard_cnt;

        shards.reserve(shard_cnt);
        for (int i = 0; i < shard_cnt; i++) {
            shards.emplace_back(std::make_unique<LRUCacheShard>(per_shard_bytes));
        }
    }

    inline LRUCacheShard* pick_shard(const std::string &key) {
        size_t h = std::hash<std::string>{}(key);
        return shards[h % shard_count].get();
    }

    bool get(const std::string &key, std::string &val) {
        auto shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard->mtx);

        auto it = shard->map.find(key);
        if (it == shard->map.end()) return false;
		log_info("cache hit");
        val = it->second->second;
        shard->lru.splice(shard->lru.begin(), shard->lru, it->second);
        return true;
    }

    void put(const std::string &key, const std::string &val) {
        auto shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard->mtx);

        size_t kv_size = key.size() + val.size() + 32;

        auto it = shard->map.find(key);
        if (it != shard->map.end()) {
            shard->current_bytes -= (it->second->first.size() + it->second->second.size() + 32);
            it->second->second = val;
            shard->current_bytes += kv_size;
            shard->lru.splice(shard->lru.begin(), shard->lru, it->second);
            return;
        }

        shard->lru.emplace_front(key, val);
        shard->map[key] = shard->lru.begin();
        shard->current_bytes += kv_size;

        while (shard->current_bytes > shard->capacity_bytes) {
            auto &last = shard->lru.back();
            shard->current_bytes -= (last.first.size() + last.second.size() + 32);
            shard->map.erase(last.first);
            shard->lru.pop_back();
        }
    }
};
