run the following commands:
make clean
make
./kv_server server.conf


here server.conf contains the configuration of the server
# server.conf
port=8080
worker_threads=3
main_thread_core=0
pin_workers=true
cache_enabled=true
cache_size_mb=10
db_host=127.0.0.1
db_user=joshi
db_pass=tadwi
db_name=smartkv
workload_mode=mix
mix_get_percent=90
log_level=info
max_conn_queue=128



here you can change the number of worker threads and the cache enable or disable.
Note: I have used ChatGPT to get a better understanding of the project, and also took its help to write the code of this project
