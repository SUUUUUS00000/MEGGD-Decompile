cmake_minimum_required(VERSION 3.10)
project(LuauDecompilerWeb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    bytecode_format.cpp
    bytecode_reader.cpp
    code_printer.cpp
    disassembler.cpp
    expr_tracker.cpp
    structurizer.cpp
    decompiler.cpp
    server.cpp
)

add_executable(luau_decompiler_server ${SOURCES})
target_compile_options(luau_decompiler_server PRIVATE -O3)

find_package(Threads REQUIRED)
target_link_libraries(luau_decompiler_server Threads::Threads)
