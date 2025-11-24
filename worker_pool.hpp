#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <unistd.h>
#include <sched.h>
#include "job.hpp"
#include "db.hpp"
#include "conn_table.hpp"
#include "config.hpp"
#include "util.hpp"
#include "lru_cache.hpp"
#include <sys/epoll.h>

extern int g_epoll_fd;

class WorkerPool {
public:
    WorkerPool(int n, DB* db_, ConnTable* ct_, LRUCache* cache_, const ServerConfig &cfg_)
        : db(db_), ct(ct_), cache(cache_), cfg(cfg_), running(true)
    {
        int threads = std::max(1, n);
        for (int i=0;i<threads;i++){
            workers.emplace_back([this,i]{ this->worker_entry(i); });
        }
        log_info("Worker pool started with " + std::to_string(threads) + " threads");
    }

    ~WorkerPool() {
        running = false;
        cv.notify_all();
        for (auto &t: workers) if (t.joinable()) t.join();
    }

    void push_job(const Job &j) {
        {
            std::lock_guard<std::mutex> lk(q_mtx);
            q.push(j);
        }
        cv.notify_one();
    }
private:
    DB *db;
    ConnTable *ct;
    LRUCache *cache;
    ServerConfig cfg;

    std::vector<std::thread> workers;
    std::mutex q_mtx;
    std::condition_variable cv;
    std::queue<Job> q;
    std::atomic<bool> running;

    void set_thread_affinity(int idx) {
        if (!cfg.pin_workers) return;
        int cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores <= 1) return;
        int main_core = cfg.main_thread_core;
        int start = (main_core + 1) % cores;
        int core = (start + idx) % cores;
        cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(core, &cpuset);
        pthread_t thr = pthread_self();
        if (pthread_setaffinity_np(thr, sizeof(cpuset), &cpuset) != 0) {
            log_error("Failed to set worker affinity to core " + std::to_string(core));
        } else {
            log_info("Worker pinned to core " + std::to_string(core));
        }
    }

    void worker_entry(int idx) {
        // set affinity for this thread if requested
        set_thread_affinity(idx);
        worker_loop(idx);
    }

    void worker_loop(int idx) {
        log_info("WORKER[" + std::to_string(idx) + "] started");
        while (running) {
            Job j;
            {
                std::unique_lock<std::mutex> lk(q_mtx);
                cv.wait(lk, [&]{ return !running || !q.empty(); });
                if (!running) break;
                j = q.front(); q.pop();
            }
            uint64_t start = now_ms();
            std::string response;

            if (j.type == Job::GET) {
                std::string val;
                bool hit = false;
                if (cfg.cache_enabled && cache) {
                    if (cache->get(j.key, val)) hit = true;
                }
                if (hit) {
                    response = "OK cache hit" + val + "\n";
                } else {
                    std::string derr;
                    bool ok = db->get(j.key, val, derr);
                    if (ok) {
                        response = "OK " + val + "\n";
                        if (cfg.cache_enabled && cache) cache->put(j.key, val);
                    } else {
                        response = "MISS\n";
                    }
                }
            } else { // PUT
                std::string derr;
                bool ok = db->put(j.key, j.value, derr);
                if (ok) {
                    response = "OK\n";
                    if (cfg.cache_enabled && cache) cache->put(j.key, j.value);
                } else {
                    response = std::string("ERR ") + derr + "\n";
                }
            }

            {
                std::lock_guard<std::mutex> g(ct->mtx);
                Conn* cp = ct->get_ptr_unlocked(j.client_fd);
                if (!cp) {
                    log_info("WORKER: client gone fd=" + std::to_string(j.client_fd));
                    continue;
                }
                bool was_not_writing = !cp->want_write;


                cp->outq.push_back(response);
                cp->want_write = true;
                if (was_not_writing) {
                    epoll_event ne{};
                    ne.events = EPOLLIN | EPOLLOUT;
                    ne.data.fd = j.client_fd;
                    if (g_epoll_fd > 0) {
                        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, j.client_fd, &ne) < 0) {
                            log_error("WORKER: epoll_ctl MOD enable EPOLLOUT failed for fd=" + std::to_string(j.client_fd));
                        } else {
                            log_info("WORKER: enabled EPOLLOUT for fd=" + std::to_string(j.client_fd));
                        }
                    } else {
                        log_error("WORKER: g_epoll_fd invalid");
                    }
                }
            }

            (void)start; // keep variable if you want to compute worker time later
        }
        log_info("WORKER exiting");
    }
};
