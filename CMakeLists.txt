cmake_minimum_required(VERSION 3.10)
project(LuauDecompilerWeb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

set(SOURCES
    src/bytecode_format.cpp
    src/bytecode_reader.cpp
    src/code_printer.cpp
    src/disassembler.cpp
    src/expr_tracker.cpp
    src/structurizer.cpp
    src/decompiler.cpp
    src/server.cpp
)

add_executable(luau_decompiler_server ${SOURCES})
target_compile_options(luau_decompiler_server PRIVATE -O3)

find_package(Threads REQUIRED)
target_link_libraries(luau_decompiler_server Threads::Threads)
