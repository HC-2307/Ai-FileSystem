cmake_minimum_required(VERSION 3.16)

project(ai_filesystem
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)

add_library(ai_fs_core STATIC
    src/block_storage.cpp
    src/block_cache.cpp
    src/predictor.cpp
    src/model_client.cpp
    src/filesystem.cpp
)
target_include_directories(ai_fs_core PUBLIC include)
target_link_libraries(ai_fs_core PRIVATE SQLite::SQLite3 PUBLIC Threads::Threads)
target_compile_options(ai_fs_core PRIVATE -Wall -Wextra)

# Version C runs python/model.py, which needs scikit-learn. Use the project's virtual
# environment when there is one.
# shortcut: the command breaks if the project path has spaces in it; quote it if that comes up.
if(EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    set(MODEL_PYTHON ${CMAKE_SOURCE_DIR}/.venv/bin/python)
else()
    set(MODEL_PYTHON python3)
endif()
target_compile_definitions(ai_fs_core PRIVATE
    AI_FS_MODEL_COMMAND="${MODEL_PYTHON} ${CMAKE_SOURCE_DIR}/python/model.py")

add_executable(ai_fs
    src/main.cpp
)
target_link_libraries(ai_fs PRIVATE ai_fs_core PkgConfig::FUSE3)
target_compile_options(ai_fs PRIVATE -Wall -Wextra)

# Runs the workloads against the library directly and prints one CSV row.
add_executable(ai_fs_bench
    tools/bench.cpp
)
target_link_libraries(ai_fs_bench PRIVATE ai_fs_core)
target_compile_options(ai_fs_bench PRIVATE -Wall -Wextra)

enable_testing()

add_executable(block_storage_test
    tests/block_storage_test.cpp
)
target_include_directories(block_storage_test PRIVATE tests)
target_link_libraries(block_storage_test PRIVATE ai_fs_core)
target_compile_options(block_storage_test PRIVATE -Wall -Wextra)
add_test(NAME block_storage_test COMMAND block_storage_test)

add_executable(filesystem_test
    tests/filesystem_test.cpp
)
target_include_directories(filesystem_test PRIVATE tests)
target_link_libraries(filesystem_test PRIVATE ai_fs_core SQLite::SQLite3)
target_compile_options(filesystem_test PRIVATE -Wall -Wextra)
add_test(NAME filesystem_test COMMAND filesystem_test)

add_executable(cache_test
    tests/cache_test.cpp
)
target_include_directories(cache_test PRIVATE tests)
target_link_libraries(cache_test PRIVATE ai_fs_core)
target_compile_options(cache_test PRIVATE -Wall -Wextra)
add_test(NAME cache_test COMMAND cache_test)

add_test(NAME model_test COMMAND ${MODEL_PYTHON} ${CMAKE_SOURCE_DIR}/tests/model_test.py)

# Mounts the filesystem for real; exits 77 (skipped) where FUSE is unavailable.
add_test(NAME mount_test COMMAND ${CMAKE_SOURCE_DIR}/tests/mount_test.sh $<TARGET_FILE:ai_fs>)
set_tests_properties(mount_test PROPERTIES SKIP_RETURN_CODE 77)
