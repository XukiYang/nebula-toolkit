#pragma once

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

namespace nebula::shmstore {

// 共享内存段类型
using ShmSegment = boost::interprocess::managed_shared_memory;

// 共享内存分配器：所有存入共享内存的容器必须使用此分配器
template <typename T>
using ShmAllocator = boost::interprocess::allocator<T, ShmSegment::segment_manager>;

// IPC 安全容器（替代 std::string / std::vector，可安全放入共享内存）
using ShmString = boost::interprocess::basic_string<char, std::char_traits<char>, ShmAllocator<char>>;

template <typename T>
using ShmVector = boost::container::vector<T, ShmAllocator<T>>;

}  // namespace nebula::shmstore
