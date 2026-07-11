cmake_minimum_required(VERSION 3.10)
project(ftp_server C)
set(CMAKE_C_STANDARD 99)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../bin)
include_directories(inc)
add_executable(ftp_server
    src/main.c
    src/handler.c
    src/protocol.c
    src/sys_auth.c
    src/ipc_shm.c
    src/thread_pool.c
)
target_link_libraries(ftp_server m pthread)
add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/ftp_server DEPENDS ftp_server)
