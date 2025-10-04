cmake_minimum_required(VERSION 3.16)

project(alloc_demo
  VERSION 1.0.0
  DESCRIPTION "STL-compatible pool allocator + custom container demo"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_WARNINGS "Enable extra warnings" ON)
option(ENABLE_LTO "Enable link-time optimization" ON)

add_executable(alloc_demo src/main.cpp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(alloc_demo PRIVATE /W4 /permissive-)
  else()
    target_compile_options(alloc_demo PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_err)
  if(ipo_supported)
    set_property(TARGET alloc_demo PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

install(TARGETS alloc_demo RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
