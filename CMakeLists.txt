cmake_minimum_required(VERSION 3.16)

project(ai_filesystem
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ai_fs_core STATIC
    src/block_storage.cpp
)
target_include_directories(ai_fs_core PUBLIC include)
target_compile_options(ai_fs_core PRIVATE -Wall -Wextra)

add_executable(ai_fs
    src/main.cpp
)
target_link_libraries(ai_fs PRIVATE ai_fs_core)
target_compile_options(ai_fs PRIVATE -Wall -Wextra)

enable_testing()

add_executable(block_storage_test
    tests/block_storage_test.cpp
)
target_include_directories(block_storage_test PRIVATE tests)
target_link_libraries(block_storage_test PRIVATE ai_fs_core)
target_compile_options(block_storage_test PRIVATE -Wall -Wextra)
add_test(NAME block_storage_test COMMAND block_storage_test)
