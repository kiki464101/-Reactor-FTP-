cmake_minimum_required(VERSION 3.10)

project(lvgl)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

link_directories(./)

# ---- External dependencies ----
find_package(PkgConfig QUIET)

# SDL2 (required for PC simulation)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 REQUIRED sdl2)
else()
    find_package(SDL2 REQUIRED)
endif()

# ---- Include paths ----
include_directories(
    ${PROJECT_SOURCE_DIR}                      # for lvgl/lvgl.h etc.
    client   # for ui_manager.h, network_task.h
    client/my_ftp/Client/inc  # for protocol.h
    freetype-2.13.3/include/
    ${SDL2_INCLUDE_DIRS}
)

# ---- Sub-projects ----
add_subdirectory(freetype-2.13.3)
add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR}
                           PRIVATE freetype-2.13.3/include/freetype)

# ---- Our source files ----
aux_source_directory(client SRC_FILE)
add_executable(main main.c mouse_cursor_icon.c ${SRC_FILE})

# ---- Link libraries ----
target_link_libraries(main
    lvgl
    lvgl::examples
    lvgl::demos
    lvgl::thorvg
    ${SDL2_LIBRARIES}
    m
    pthread
    freetype
)

add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)
# ---- Server (optional, standalone binary) ----
# Uncomment the line below if you want to build the server together:
# add_subdirectory(server)
