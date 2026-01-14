# Shuai-KV 测试用例

# 使用 FetchContent 自动获取 GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# 对于 Windows，需要禁用默认的 gtest_build_samples
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_disable_pthreads OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 启用测试
enable_testing()

# 测试源文件
set(TEST_SOURCES
    skip_list_test.cpp
    db_test.cpp
    lock_test.cpp
    thread_pool_test.cpp
    list_test.cpp
    bloom_filter_test.cpp
    cache_test.cpp
    compaction_test.cpp
    cm_sketch_test.cpp
)

# 创建测试可执行文件
foreach(source ${TEST_SOURCES})
    get_filename_component(test_name ${source} NAME_WE)
    add_executable(${test_name} ${source})
    # 注意：静态库链接顺序，shuaikv 放在最后以确保正确解析符号
    target_link_libraries(${test_name}
        PRIVATE
            GTest::gtest_main
            shuaikv_grpc
            gRPC::grpc++
            grpc
            protobuf::libprotobuf
            shuaikv
    )
    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_BINARY_DIR}/shuaikv
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

message(STATUS "Tests configured: ${TEST_SOURCES}")
message(STATUS "Benchmark: benchmark.cpp")
