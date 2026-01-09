cc_library(
    name = "shuai-kv",
    srcs = glob([
        "utils/*.cpp",
    ]),
    hdrs = glob([
        "cache/*.hpp",
        "cache/*.h",
        "pool/*.hpp",
        "utils/*.h",
        "utils/*.hpp",
        "lsm/*.hpp",
        "raft/*.hpp",
        "*.hpp",
    ]),
    deps = [
        "//SHUAI-KV/raft/protos:cc_raft_grpc"
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "server",
    srcs = [
        "server.cpp",
    ],
    deps = [
        ":shuai-kv"
    ]
)

cc_binary(
    name = "client",
    srcs = [
        "client.cpp",
    ],
    deps = [
        ":shuai-kv"
    ]
)
