#pragma once
#include "conn.hpp"
#include <unordered_map>
#include <mutex>

class ConnTable {
public:
    std::unordered_map<int, Conn> map;
    std::mutex mtx;

    // create entry on accept
    void add(int fd) {
        std::lock_guard<std::mutex> g(mtx);
        Conn c; c.fd = fd;
        map[fd] = std::move(c);
    }
    // remove entry on close
    void remove_fd(int fd) {
        std::lock_guard<std::mutex> g(mtx);
        map.erase(fd);
    }
    // get pointer to conn or nullptr; DOES NOT create
    Conn* get_ptr_unlocked(int fd) {
        auto it = map.find(fd);
        if (it == map.end()) return nullptr;
        return &(it->second);
    }

    Conn* get_ptr(int fd) {
        std::lock_guard<std::mutex> g(mtx);
        return get_ptr_unlocked(fd);
    }
    bool exists(int fd) {
        std::lock_guard<std::mutex> g(mtx);
        return map.find(fd) != map.end();
    }
};
