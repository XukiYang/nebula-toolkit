# ── 第三方库示例 ──────────────────────────────────────────
nebula_add_example(example_boost_multi_index ${CMAKE_CURRENT_SOURCE_DIR}/boost/multi_index.cpp)
nebula_add_example(example_boost_managed_shm ${CMAKE_CURRENT_SOURCE_DIR}/boost/managed_shared_memory.cpp)
nebula_add_example(example_fmt ${CMAKE_CURRENT_SOURCE_DIR}/fmt/fmt_demo.cpp)
nebula_add_example(example_nlohmann_json ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/json_demo.cpp)

# ── nebula 模块示例 ──────────────────────────────────────
# containers
nebula_add_example(example_circular_buffer ${CMAKE_CURRENT_SOURCE_DIR}/containers/circular_buffer_demo.cpp)
nebula_add_example(example_bytes_stream ${CMAKE_CURRENT_SOURCE_DIR}/containers/bytes_stream_demo.cpp)
nebula_add_example(example_lockfree_queue ${CMAKE_CURRENT_SOURCE_DIR}/containers/lockfree_queue_demo.cpp)
nebula_add_example(example_unpacker ${CMAKE_CURRENT_SOURCE_DIR}/containers/unpacker_demo.cpp)

# memory
nebula_add_example(example_memory_pool ${CMAKE_CURRENT_SOURCE_DIR}/memory/memory_pool_demo.cpp)

# crypto
nebula_add_example(example_crc32 ${CMAKE_CURRENT_SOURCE_DIR}/crypto/crc32_demo.cpp)

# config_handler
nebula_add_example(example_config_handler ${CMAKE_CURRENT_SOURCE_DIR}/config_handler/config_handler_demo.cpp)

# logger
nebula_add_example(example_logger ${CMAKE_CURRENT_SOURCE_DIR}/logger/logger_demo.cpp)

# threading
nebula_add_example(example_timer_scheduler ${CMAKE_CURRENT_SOURCE_DIR}/threading/timer_scheduler_demo.cpp)

# io
nebula_add_example(example_reactor_tcp_echo ${CMAKE_CURRENT_SOURCE_DIR}/io/reactor_tcp_echo_demo.cpp)

# shmstore
nebula_add_example(example_shmstore_sender ${CMAKE_CURRENT_SOURCE_DIR}/shmstore/sender.cpp)
nebula_add_example(example_shmstore_receiver ${CMAKE_CURRENT_SOURCE_DIR}/shmstore/receiver.cpp)
