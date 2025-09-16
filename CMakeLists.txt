cmake_minimum_required(VERSION 3.15)
project(node-process-watcher)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码文件
set(SRC_FILES
        src/c/linux_process.cc
        src/c/mac_process.cc
        src/c/win_process.cc
)



# 获取 node-addon-api include 路径
execute_process(
        COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

#include_directories(
#        /Users/mac/.nvm/versions/node/v22.0.0/include/node
#        /Users/mac/CLionProjects/node-process-watcher/node_modules/node-addon-api
#)
# 获取 Node.js 内置 include 路径
execute_process(
        COMMAND node -p "require('node:process').execPath"
        OUTPUT_VARIABLE NODE_EXEC_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
get_filename_component(NODE_INCLUDE_DIR "${NODE_EXEC_PATH}/../../include/node" ABSOLUTE)
# 打印变量值
string(REPLACE "\"" "" NODE_ADDON_API_INCLUDE "${NODE_ADDON_API_INCLUDE}")
message(STATUS "node-addon-api 是 ${NODE_ADDON_API_INCLUDE}")
message(STATUS "node 是 ${NODE_INCLUDE_DIR}")
# 让 CLion 索引头文件
include_directories(
        ${NODE_INCLUDE_DIR}
        ${NODE_ADDON_API_INCLUDE}
)

# 把源文件添加进库才能进行编译 或者add_executable但是不需要
add_library(addon_dummy STATIC ${SRC_FILES})

# add_library配合使用
#target_include_directories(addon_dummy PRIVATE ${NODE_INCLUDE_DIR})
