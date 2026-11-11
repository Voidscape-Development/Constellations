cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/common.c
    src/lattice.c
    src/pattern-media.c
    src/pattern-renderer.c
    src/pattern-settings.c
    src/pattern-source.c
    src/pattern-filter.c
    src/pattern-border.c
    src/skylines-source.c
    src/text-tiles-math.c
    src/nanosvg-impl.c
    src/transition-common.c
    src/presets.c
    src/tiles-math.c
    src/tiles-transition.c
    src/vortex-math.c
    src/vortex-transition.c
    src/glass-mesh.c
    src/glass-transition.c
    src/matte-math.c
    src/matte-transition.c
    src/portal-math.c
    src/portal-transition.c
)

# Text Tiles lays its text out with Qt, so it is only built with Qt enabled
# (the presets enable it).
if(ENABLE_QT)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/text-tiles-source.c src/text-tiles-raster.cpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CONSTELLATIONS_HAS_QT)
endif()

if(NOT MSVC)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps/nanosvg")
set_source_files_properties(src/nanosvg-impl.c PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>")

# Checks that render the transition effects on a real graphics device, so they
# need libobs and are built with the plugin rather than in tests/. They carry
# the "gpu" label; on a headless machine run them under xvfb-run:
# `xvfb-run -a ctest -L gpu`. The unit tests that need no OBS SDK live in
# tests/CMakeLists.txt.
option(ENABLE_GPU_TESTS "Build the effect checks that render on a real graphics device" OFF)

if(ENABLE_GPU_TESTS)
  enable_testing()

  function(add_gpu_check name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE OBS::libobs)
    target_include_directories(${name} PRIVATE src tests)
    if(NOT MSVC)
      target_link_libraries(${name} PRIVATE m)
    endif()
    if(APPLE)
      target_compile_definitions(${name} PRIVATE GPU_CHECK_GRAPHICS_MODULE="libobs-opengl.dylib")
    elseif(WIN32)
      target_compile_definitions(${name} PRIVATE GPU_CHECK_GRAPHICS_MODULE="libobs-d3d11.dll")
    endif()
  endfunction()

  # Sprite effects, each with uniforms and a schedule from its own math. The
  # glass transition draws a mesh rather than a full screen sprite, so its
  # check builds the shards with glass-mesh.c.
  add_gpu_check(gpu-check tests/gpu-check.c src/tiles-math.c)
  add_gpu_check(glass-check tests/glass-check.c src/glass-mesh.c)
  foreach(check IN ITEMS matte portal vortex)
    add_gpu_check(${check}-check tests/${check}-check.c src/${check}-math.c)
  endforeach()

  # Each check renders the effect of the same name, but for the tiling one,
  # which was the first and is called gpu-check.
  foreach(
    effect
    IN
    ITEMS tiles glass matte portal vortex
  )
    if(effect STREQUAL "tiles")
      set(exe gpu)
    else()
      set(exe ${effect})
    endif()
    add_test(
      NAME ${exe}-check
      COMMAND ${exe}-check "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/${effect}_transition.effect"
    )
    set_tests_properties(${exe}-check PROPERTIES LABELS gpu)
  endforeach()

  # Loads the plugin module itself and drives each transition through libobs,
  # so it runs the plugin's own code between the settings and the effects.
  add_gpu_check(plugin-check tests/plugin-check.c)
  add_dependencies(plugin-check ${CMAKE_PROJECT_NAME})
  add_test(
    NAME plugin-check
    COMMAND plugin-check "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_SOURCE_DIR}/data"
  )
  set_tests_properties(plugin-check PROPERTIES LABELS gpu)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
