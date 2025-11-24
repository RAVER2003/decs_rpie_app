#pragma once
#include <chrono>
#include <iostream>
#include <string>

inline uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void log_info(const std::string &s) {
    std::cout << "[INFO] " << s << std::endl;
}
inline void log_error(const std::string &s) {
    std::cerr << "[ERROR] " << s << std::endl;
}
