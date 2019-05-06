cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")
project(lightvis-depends VERSION 0.1.0 LANGUAGES CXX)

set(BUILD_EIGEN ON)
set(BUILD_GLFW ON)
set(BUILD_GLBINDING ON)
set(BUILD_NUKLEAR ON)

include(FetchContent)

function(expects_error target dependency)
  message(FATAL_ERROR "${target} expects ${dependency}.")
endfunction()

function(force_build_error target)
  message(FATAL_ERROR "${target} must be built from source.")
endfunction()

if(NOT TARGET options::cpp17)
  add_library(options::cpp17 INTERFACE IMPORTED GLOBAL)
  target_compile_features(options::cpp17 INTERFACE cxx_std_17)
endif()

if(NOT TARGET options::moderncpp)
  if(TARGET options::cpp11)
    add_library(options::moderncpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(options::moderncpp INTERFACE options::cpp11)
  elseif(TARGET options::cpp14)
    add_library(options::moderncpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(options::moderncpp INTERFACE options::cpp14)
  elseif(TARGET options::cpp17)
    add_library(options::moderncpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(options::moderncpp INTERFACE options::cpp17)
  else()
    expects_error(options::moderncpp "options::cpp11 or above")
  endif()
endif()

if(NOT TARGET depends::eigen)
  add_library(depends::eigen INTERFACE IMPORTED GLOBAL)
  if(TARGET options::moderncpp)
    target_link_libraries(depends::eigen INTERFACE options::moderncpp)
  else()
    expects_error(depends::eigen options::moderncpp)
  endif()
  if(BUILD_EIGEN)
    FetchContent_Declare(
      depends-eigen3
      GIT_REPOSITORY https://github.com/eigenteam/eigen-git-mirror.git
      GIT_TAG        3.3.7
    )
    FetchContent_GetProperties(depends-eigen3)
    if(NOT depends-eigen3_POPULATED)
      message(STATUS "Fetching Eigen3 sources")
      FetchContent_Populate(depends-eigen3)
      message(STATUS "Fetching Eigen3 sources - done")
    endif()
    target_include_directories(depends::eigen INTERFACE ${depends-eigen3_SOURCE_DIR})
  else()
    find_package(Eigen3 REQUIRED)
    target_link_libraries(depends::eigen INTERFACE Eigen3::Eigen)
  endif()
endif()

if(NOT TARGET depends::opengl)
  add_library(depends::opengl INTERFACE IMPORTED GLOBAL)
  find_package(OpenGL REQUIRED)
  target_include_directories(depends::opengl INTERFACE ${OPENGL_INCLUDE_DIR})
  target_link_libraries(depends::opengl INTERFACE ${OPENGL_gl_LIBRARY})
endif()

if(NOT TARGET depends::glfw)
  add_library(depends::glfw INTERFACE IMPORTED GLOBAL)
  if(TARGET depends::opengl)
    target_link_libraries(depends::glfw INTERFACE depends::opengl)
  else()
    message(FATAL_ERROR "depends::glfw expects depends::opengl.")
  endif()
  if(BUILD_GLFW)
    FetchContent_Declare(
      depends-glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG        3.3
    )
    FetchContent_GetProperties(depends-glfw)
    if(NOT depends-glfw_POPULATED)
      message(STATUS "Fetching GLFW sources")
      FetchContent_Populate(depends-glfw)
      message(STATUS "Fetching GLFW sources - done")
    endif()
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${depends-glfw_SOURCE_DIR} ${depends-glfw_BINARY_DIR})
  else()
    find_package(glfw3 REQUIRED)
  endif()
  target_link_libraries(depends::glfw INTERFACE glfw)
endif()

if(NOT TARGET depends::glbinding)
  add_library(depends::glbinding INTERFACE IMPORTED GLOBAL)
  if(BUILD_GLBINDING)
    FetchContent_Declare(
      depends-glbinding
      GIT_REPOSITORY https://github.com/cginternals/glbinding.git
      GIT_TAG        v3.1.0
    )
    FetchContent_GetProperties(depends-glbinding)
    if(NOT depends-glbinding_POPULATED)
      message(STATUS "Fetching glbinding sources")
      FetchContent_Populate(depends-glbinding)
      message(STATUS "Fetching glbinding sources - done")
    endif()
    set(OPTION_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(OPTION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${depends-glbinding_SOURCE_DIR} ${depends-glbinding_BINARY_DIR})
    find_package(glbinding CONFIG REQUIRED PATHS "${depends-glbinding_SOURCE_DIR}")
    target_link_libraries(depends::glbinding INTERFACE glbinding::glbinding glbinding::glbinding-aux)
  else()
    force_build_error(depends::glbinding)
  endif()
endif()

if(NOT TARGET depends::nuklear)
  add_library(depends::nuklear INTERFACE IMPORTED GLOBAL)
  if(BUILD_NUKLEAR)
    FetchContent_Declare(
      depends-nuklear
      GIT_REPOSITORY https://github.com/vurtun/nuklear.git
      GIT_TAG        509c75b
    )
    FetchContent_GetProperties(depends-nuklear)
    if(NOT depends-nuklear_POPULATED)
      message(STATUS "Fetching nuklear sources")
      FetchContent_Populate(depends-nuklear)
      message(STATUS "Fetching nuklear sources - done")
    endif()
    target_include_directories(depends::nuklear INTERFACE ${depends-nuklear_SOURCE_DIR})
    target_compile_definitions(depends::nuklear INTERFACE
      NK_INCLUDE_FIXED_TYPES
      NK_INCLUDE_DEFAULT_ALLOCATOR
      NK_INCLUDE_STANDARD_IO
      NK_INCLUDE_STANDARD_VARARGS
      NK_INCLUDE_VERTEX_BUFFER_OUTPUT
      NK_INCLUDE_FONT_BAKING
      NK_INCLUDE_DEFAULT_FONT
      NK_KEYSTATE_BASED_INPUT
    )
  else()
    force_build_error(depends::nuklear)
  endif()
endif()
