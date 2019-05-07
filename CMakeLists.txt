cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")
project(lightvis VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

add_subdirectory(depends)

add_library(lightvis
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lightvis/lightvis.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/lightvis/lightvis.cpp
)

target_include_directories(lightvis
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(lightvis
  PUBLIC
    depends::eigen
    depends::glfw
    depends::glbinding
    depends::nuklear
)

export(TARGETS lightvis NAMESPACE lightvis:: FILE lightvisConfig.cmake)
