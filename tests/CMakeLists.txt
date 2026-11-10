# Unit tests for the parts of the plugin that do not need libobs. Built on
# their own so CI (and contributors) can run them without an OBS SDK:
#
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
cmake_minimum_required(VERSION 3.28...3.30)

project(constellations-tests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

enable_testing()

add_executable(test-lattice test-lattice.c ../src/lattice.c)
target_include_directories(test-lattice PRIVATE ../src)
target_compile_options(test-lattice PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>)
target_link_libraries(test-lattice PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>)
add_test(NAME lattice COMMAND test-lattice)

# Transition math. The *-math.c files hold the CPU side of each transition;
# the shader tests compile the effect's *_math.effect as C++ through hlsl.hpp,
# so they check the code the shader runs rather than a copy of it. HLSL
# literals are single precision, and with GCC so are these.
function(add_transition_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ../src . ../data/effects)
  target_link_libraries(${name} PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

function(add_shader_test name)
  add_transition_test(${name} ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_compile_options(${name} PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fsingle-precision-constant>)
endfunction()

add_shader_test(test-tiling test-tiling.cpp ../src/tiles-math.c)
add_shader_test(test-vortex test-vortex.cpp ../src/vortex-math.c)
add_shader_test(test-portal test-portal.cpp ../src/portal-math.c)

# The matte field and the shard motion are needed on the CPU as well, by the
# plugin itself, so they exist in C too; this holds each to its shader.
add_shader_test(test-mirrors test-mirrors.cpp ../src/matte-math.c ../src/glass-mesh.c)

# The glass transition builds its shards in C rather than in a shader, and the
# matte transition its clock and its range, so these link that code directly.
add_transition_test(test-glass test-glass.c ../src/glass-mesh.c)
add_transition_test(test-matte test-matte.c ../src/matte-math.c)
