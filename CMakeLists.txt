cmake_minimum_required(VERSION 3.16)
project(Shuai-KV
    VERSION 0.1.0
    DESCRIPTION "A distributed key-value store based on LSM Tree and Raft consensus"
    LANGUAGES CXX
)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找 gRPC 和 Protobuf
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# 打印配置信息
message(STATUS "gRPC version: ${gRPC_VERSION}")
message(STATUS "Protobuf version: ${Protobuf_VERSION}")

# 添加子目录
add_subdirectory(shuaikv)

# 测试
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# 安装配置
include(GNUInstallDirs)

install(TARGETS shuaikv_server shuaikv_client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES raft.cfg
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/shuaikv
)

message(STATUS "Build configuration complete")
message(STATUS "Use: cmake --build <build_dir> --target shuaikv_server")
