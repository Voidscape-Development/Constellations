# Unit tests for the parts of the plugin that do not need libobs. Built on
# their own so CI (and contributors) can run them without an OBS SDK:
#
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
cmake_minimum_required(VERSION 3.28...3.30)

project(constellations-tests LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

enable_testing()

add_executable(test-lattice test-lattice.c ../src/lattice.c)
target_include_directories(test-lattice PRIVATE ../src)
target_compile_options(test-lattice PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>)
target_link_libraries(test-lattice PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>)
add_test(NAME lattice COMMAND test-lattice)
