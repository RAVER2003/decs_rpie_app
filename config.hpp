#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <cctype>

static inline std::string trim(const std::string &s) {
    size_t a=0,b=s.size();
    while (a<b && std::isspace((unsigned char)s[a])) ++a;
    while (b> a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a,b-a);
}

struct ServerConfig {
    int port = 8080;
    int worker_threads = 3;
    int main_thread_core = 0;
    bool pin_workers = false;
    bool cache_enabled = true;
    int cache_size_mb = 10;
    std::string db_host = "127.0.0.1";
    std::string db_user = "joshi";
    std::string db_pass = "tadwi";
    std::string db_name = "smartkv";
    std::string workload_mode = "mix";
    int mix_get_percent = 90;
    std::string log_level = "info";
    int max_conn_queue = 128;
};

static inline std::unordered_map<std::string,std::string> parse_kv_file(const std::string &path) {
    std::unordered_map<std::string,std::string> m;
    std::ifstream f(path);
    if(!f.is_open()) return m;
    std::string line;
    while(std::getline(f,line)) {
        auto cpos = line.find('#'); if (cpos!=std::string::npos) line = line.substr(0,cpos);
        auto pos = line.find('='); if (pos==std::string::npos) continue;
        std::string k = trim(line.substr(0,pos));
        std::string v = trim(line.substr(pos+1));
        if(!k.empty()) m[k]=v;
    }
    return m;
}

static inline bool str_to_bool(const std::string &s) {
    std::string x=s; std::transform(x.begin(), x.end(), x.begin(), ::tolower);
    return x=="1"||x=="true"||x=="yes"||x=="on";
}

static inline int stoi_def(const std::unordered_map<std::string,std::string>&m,const std::string &k,int def) {
    auto it=m.find(k); if(it==m.end()) return def;
    try { return std::stoi(it->second); } catch(...) { return def; }
}
static inline std::string str_def(const std::unordered_map<std::string,std::string>&m,const std::string &k,const std::string &def) {
    auto it=m.find(k); if(it==m.end()) return def; return it->second;
}

static inline ServerConfig load_config_file(const std::string &path) {
    ServerConfig cfg;
    auto m = parse_kv_file(path);
    cfg.port = stoi_def(m,"port",cfg.port);
    cfg.worker_threads = stoi_def(m,"worker_threads",cfg.worker_threads);
    cfg.main_thread_core = stoi_def(m,"main_thread_core",cfg.main_thread_core);
    cfg.pin_workers = str_to_bool(str_def(m,"pin_workers", cfg.pin_workers ? "true":"false"));
    cfg.cache_enabled = str_to_bool(str_def(m,"cache_enabled", cfg.cache_enabled ? "true":"false"));
    cfg.cache_size_mb = stoi_def(m,"cache_size_mb", cfg.cache_size_mb);
    cfg.db_host = str_def(m,"db_host", cfg.db_host);
    cfg.db_user = str_def(m,"db_user", cfg.db_user);
    cfg.db_pass = str_def(m,"db_pass", cfg.db_pass);
    cfg.db_name = str_def(m,"db_name", cfg.db_name);
    cfg.workload_mode = str_def(m,"workload_mode", cfg.workload_mode);
    cfg.mix_get_percent = stoi_def(m,"mix_get_percent", cfg.mix_get_percent);
    cfg.log_level = str_def(m,"log_level", cfg.log_level);
    cfg.max_conn_queue = stoi_def(m,"max_conn_queue", cfg.max_conn_queue);
    return cfg;
}
