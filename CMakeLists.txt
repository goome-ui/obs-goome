cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(goome-native OBJECT src/goome-impl.c)
target_include_directories(goome-native PRIVATE native)
set_target_properties(goome-native PROPERTIES POSITION_INDEPENDENT_CODE ON COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(goome-native PRIVATE $<$<C_COMPILER_ID:MSVC>:/W0> $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs goome-native ${CMAKE_DL_LIBS})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE native src)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-comma -Wno-shorten-64-to-32>
    $<$<C_COMPILER_ID:MSVC>:/wd4244 /wd4267 /wd4305 /wd4018>
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.c src/goome-source.c src/overlay.c src/builder.c src/json.c
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
