#include <iostream>
#include "include/containers/circular_buffer.hpp"
#include "include/containers/unpacker.hpp"
#include "include/logger/logger.hpp"
#include "include/threading/thread_pool.hpp"

int main() {
    // 测试 CircularBuffer
    nebula::containers::CircularBuffer buffer(1024);
    std::string test_data = "Hello, Nebula!";
    buffer.Write(test_data);
    
    std::string read_data;
    buffer.Read(read_data, test_data.size());
    std::cout << "CircularBuffer test: " << read_data << std::endl;
    
    // 测试 Logger
    nebula::logger::Logger::Instance().LogCout(nebula::logger::Logger::INFO, __func__, __LINE__, "Logger test");
    
    // 测试 ThreadPool
    nebula::threading::ThreadPool pool(2);
    pool.PostTask([]() {
        std::cout << "ThreadPool task executed" << std::endl;
        return 0;
    });
    
    std::cout << "Namespace test completed successfully!" << std::endl;
    return 0;
}