#pragma once
#include <string>
struct Job {
    enum Type { GET=0, PUT=1 } type;
    int client_fd = -1;
    std::string key;
    std::string value;
    uint64_t enqueue_ts = 0;
};
