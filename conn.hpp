#pragma once
#include <string>
#include <deque>

struct Conn {
    int fd = -1;
    std::string inbuf;
    std::deque<std::string> outq;
    bool want_write = false;
};
