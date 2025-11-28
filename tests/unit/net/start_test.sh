g++ -o net_benchmark_test net_benchmark_test.cpp -pthread

# TCP 模式：./net_benchmark_test tcp <并发连接数> <每包负载字节数> <每连接每秒发送次数> [<持续秒数（默认10）>]
# UDP 模式：./net_benchmark_test udp <发送线程数> <每包负载字节数> [<总包数（设为0则按持续时间发送）>] [<持续秒数（默认10）>]
./net_benchmark_test tcp 10 200 300 10
