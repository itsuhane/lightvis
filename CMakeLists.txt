cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")
project(lightvis VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(SuperBuildDepends)

superbuild_option(cpp17)
superbuild_option(modern-cpp)
superbuild_depend(eigen)
superbuild_depend(glfw)
superbuild_depend(glbinding)
superbuild_depend(nuklear)

add_library(lightvis
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lightvis/lightvis.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/lightvis/lightvis.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/lightvis/lightvis_font_roboto.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/lightvis/lightvis_font_roboto.cpp
)

target_include_directories(lightvis
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(lightvis
  PRIVATE
    depends::eigen
    depends::glfw
    depends::glbinding
    depends::nuklear
)
