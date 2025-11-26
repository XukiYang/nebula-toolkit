g++ -o net_benchmark_test net_benchmark_test.cpp -pthread

# TCP 模式：tcp <并发连接数> <每包负载字节数> <每连接发送次数> [<持续秒数>]
# UDP 模式：udp <发送线程数> <每包负载字节数> [<总包数>] [<持续秒数>]

# TCP 测试：100 并发，每连接发 1000 包，payload=200 字节
./net_benchmark_test tcp 10 200 1000

# # TCP 测试：持续压测 30 秒（不限请求数）
# ./net_benchmark_test tcp 200 512 0 30
# # UDP 测试：4 线程，持续发 10 秒，payload=1024
# ./net_benchmark_test udp 4 1024 0 10
# # UDP 测试：发 50000 个包后停止
# ./net_benchmark_test udp 8 64 50000 0