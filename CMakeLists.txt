cmake_minimum_required(VERSION 3.20)

project(ydict LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Library: ydict (mock for now) ----
add_library(ydict STATIC
    src/ydict/ydict.cpp
)

target_include_directories(ydict PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Reasonable warnings (MSVC + others)
target_compile_options(ydict PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive- /Zc:__cplusplus
        /utf-8
        /Zc:char8_t-
    >
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# ---- App: ydict_app (mock GUI later) ----
add_executable(ydict_app
    src/ydict/main.cpp
)

target_link_libraries(ydict_app PRIVATE ydict)

# Nice: make app the default startup target in some IDEs
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ydict_app)

# ---- Default config next to the exe (copied only if missing) ----
set(YDICT_DEFAULT_CFG "${CMAKE_CURRENT_SOURCE_DIR}/cfg/ydict.cfg.example")
set(YDICT_COPY_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake")

add_custom_command(TARGET ydict_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            -Dsrc="${YDICT_DEFAULT_CFG}"
            -Ddst="$<TARGET_FILE_DIR:ydict_app>/ydict.cfg"
            -P "${YDICT_COPY_SCRIPT}"
    COMMENT "Ensuring ydict.cfg exists next to ydict_app.exe (copy only if missing)"
)
