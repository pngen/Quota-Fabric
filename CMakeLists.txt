cmake_minimum_required(VERSION 3.24)

project(QuotaFabric
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral runtime for governing multi-tenant accelerator quotas"
  HOMEPAGE_URL "https://github.com/pngen/Quota-Fabric"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(QUOTAFABRIC_BUILD_TESTS      "Build Quota Fabric tests"        ON)
option(QUOTAFABRIC_BUILD_EXAMPLES   "Build Quota Fabric examples"     ON)
option(QUOTAFABRIC_BUILD_BENCHMARKS "Build Quota Fabric benchmarks"   ON)
option(QUOTAFABRIC_BUILD_TOOLS      "Build coordinator/agent/ctl apps" ON)
option(QUOTAFABRIC_ENABLE_CUDA      "Enable the real CUDA backend"     ON)
option(QUOTAFABRIC_BUILD_SHARED     "Build shared (DLL) library"       OFF)
option(QUOTAFABRIC_FATAL_WARNINGS   "Treat compiler warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Language / compiler configuration
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Warnings. We scope warning flags to CXX so they are NEVER forwarded to nvcc.
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
  if(QUOTAFABRIC_FATAL_WARNINGS)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
  if(QUOTAFABRIC_FATAL_WARNINGS)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror>)
  endif()
endif()

# ---------------------------------------------------------------------------
# CUDA backend detection
# ---------------------------------------------------------------------------
set(QuotaFabric_CUDA_FOUND FALSE)
if(QUOTAFABRIC_ENABLE_CUDA)
  enable_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES "120")
    set(QuotaFabric_CUDA_FOUND TRUE)
    message(STATUS "QuotaFabric: CUDA backend enabled (nvcc=${CMAKE_CUDA_COMPILER})")
    if(MSVC)
      add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/EHsc>)
    endif()
  else()
    message(STATUS "QuotaFabric: QUOTAFABRIC_ENABLE_CUDA is ON but no nvcc found; disabling")
  endif()
else()
  message(STATUS "QuotaFabric: CUDA backend disabled")
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_subdirectory(src)
