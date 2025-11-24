#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <cstring>

#include "util.hpp"
#include "config.hpp"
#include "conn_table.hpp"
#include "worker_pool.hpp"
#include "db.hpp"
#include "lru_cache.hpp"

int g_epoll_fd = -1;
static volatile bool g_running = true;
static void sigint_handler(int) { g_running = false; }

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void pin_main_thread(const ServerConfig &cfg) {
    if (!cfg.pin_workers) return;
    int cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0) return;
    int core = cfg.main_thread_core % cores;
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(core, &cpuset);
    pthread_t thr = pthread_self();
    if (pthread_setaffinity_np(thr, sizeof(cpuset), &cpuset) != 0) {
        log_error("Failed to pin main thread to core " + std::to_string(core));
    } else {
        log_info("Main thread pinned to core " + std::to_string(core));
    }
}

int main(int argc, char** argv) {
    std::string cfg_path = "server.conf";
    if (argc > 1) cfg_path = argv[1];

    ServerConfig cfg = load_config_file(cfg_path);
    log_info("Config: port=" + std::to_string(cfg.port) + " workers=" + std::to_string(cfg.worker_threads)
             + " cache=" + (cfg.cache_enabled?"on":"off") + " cache_mb=" + std::to_string(cfg.cache_size_mb)
             + " pin_workers=" + (cfg.pin_workers ? "true":"false"));

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    // DB init
    DB db;
    if (!db.connect(cfg.db_host.c_str(), cfg.db_user.c_str(), cfg.db_pass.c_str(), cfg.db_name.c_str())) {
        log_error("DB connect failed. Exiting.");
        return 1;
    }

    // optional cache
    LRUCache cache(4, (size_t)cfg.cache_size_mb * 1024 * 1024);

    ConnTable ct;

    // create epoll and expose global
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) { perror("epoll_create1"); return 1; }
    int ep = g_epoll_fd;

    // pin main thread if requested
    pin_main_thread(cfg);

    // start worker pool
    WorkerPool pool(cfg.worker_threads, &db, &ct, (cfg.cache_enabled?&cache:nullptr), cfg);

    // listen
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int yes = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(cfg.port); addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, cfg.max_conn_queue) < 0) { perror("listen"); return 1; }
    set_nonblocking(listen_fd);

    epoll_event lev{}; lev.events = EPOLLIN; lev.data.fd = listen_fd;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, listen_fd, &lev) < 0) { perror("epoll_ctl add listen"); return 1; }

    log_info("Server listening on port " + std::to_string(cfg.port));

    const int MAX_EVENTS = 256;
    std::vector<epoll_event> events(MAX_EVENTS);

    while (g_running) {
        int n = epoll_wait(ep, events.data(), MAX_EVENTS, 1000);
        if (n < 0) { if (errno == EINTR) continue; perror("epoll_wait"); break; }
        if (n == 0) continue;

        for (int i=0;i<n;i++) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == listen_fd) {
                // accept loop non-blocking
                while (true) {
                    sockaddr_in cli{}; socklen_t clilen = sizeof(cli);
                    int c = accept(listen_fd, (sockaddr*)&cli, &clilen);
                    if (c < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_error(std::string("accept: ") + strerror(errno));
                        break;
                    }
                    set_nonblocking(c);
                    ct.add(c); // create conn entry only here
                    epoll_event cev{}; cev.events = EPOLLIN; cev.data.fd = c;
                    if (epoll_ctl(ep, EPOLL_CTL_ADD, c, &cev) < 0) {
                        log_error(std::string("epoll_ctl ADD client failed: ") + strerror(errno));
                        close(c); ct.remove_fd(c);
                    } else {
                        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                        log_info("Accepted fd=" + std::to_string(c) + " from " + std::string(ip));
                    }
                }
                continue;
            }

            if (evs & (EPOLLERR | EPOLLHUP)) {
                log_info("EPOLLERR/HUP on fd=" + std::to_string(fd) + " closing");
                close(fd); ct.remove_fd(fd);
                continue;
            }

            if (evs & EPOLLIN) {
                Conn* cp = ct.get_ptr(fd);
                if (!cp) { log_info("EPOLLIN but no conn for fd=" + std::to_string(fd)); continue; }
                while (true) {
                    char buf[4096];
                    ssize_t r = recv(fd, buf, sizeof(buf), 0);
                    if (r > 0) {
                        cp->inbuf.append(buf, r);
                        size_t pos;
                        while ((pos = cp->inbuf.find('\n')) != std::string::npos) {
                            std::string line = cp->inbuf.substr(0, pos);
                            cp->inbuf.erase(0, pos+1);
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                                log_info("PARSER: stripped CR");
                            }
                            log_info("PARSER: '" + line + "'");
                            Job j; j.client_fd = fd; j.enqueue_ts = now_ms();
                            if (line.rfind("GET ", 0) == 0) {
                                j.type = Job::GET; j.key = line.substr(4); pool.push_job(j);
                            } else if (line.rfind("PUT ", 0) == 0) {
                                size_t sp = line.find(' ', 4);
                                if (sp == std::string::npos) continue;
                                j.type = Job::PUT;
                                j.key = line.substr(4, sp-4);
                                j.value = line.substr(sp+1);
                                pool.push_job(j);
                            } else {
                                log_info("Unknown command: '" + line + "'");
                            }
                        }
                    } else if (r == 0) {
                        log_info("Client closed fd=" + std::to_string(fd));
                        close(fd); ct.remove_fd(fd);
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_error(std::string("recv error: ") + strerror(errno));
                        close(fd); ct.remove_fd(fd);
                        break;
                    }
                }
            }

            if (evs & EPOLLOUT) {
                Conn* cp = ct.get_ptr(fd);
                if (!cp) { log_info("EPOLLOUT but no conn for fd=" + std::to_string(fd)); continue; }
                std::lock_guard<std::mutex> g(ct.mtx);
                while (!cp->outq.empty()) {
                    std::string &msg = cp->outq.front();
                    ssize_t w = send(fd, msg.data(), msg.size(), 0);
                    if (w < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_error("send failed: " + std::string(strerror(errno)));
                        close(fd); ct.remove_fd(fd); break;
                    }
                    if ((size_t)w < msg.size()) { msg.erase(0, w); break; }
                    cp->outq.pop_front();
                }
                if (cp->outq.empty()) {
                    cp->want_write = false;
                    epoll_event ne{}; ne.events = EPOLLIN; ne.data.fd = fd;
                    if (epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ne) < 0) {
                        log_error("epoll_ctl MOD disable EPOLLOUT failed for fd=" + std::to_string(fd));
                    }
                }
            }
        } // for events
    } // while running

    log_info("Shutting down server...");
    close(listen_fd);
    close(ep);
    return 0;
}
