# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
file(GLOB QUOTAFABRIC_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/policy/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/accounting/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/hierarchy/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/reservation/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/borrowing/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/persistence/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/backends/cpu/*.cpp"
)

# External packages needed by the core library
find_package(Threads REQUIRED)

add_library(quota_fabric_core STATIC ${QUOTAFABRIC_CORE_SOURCES})
add_library(QuotaFabric::core ALIAS quota_fabric_core)
set_target_properties(quota_fabric_core PROPERTIES EXPORT_NAME core)
target_include_directories(quota_fabric_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(quota_fabric_core PUBLIC Threads::Threads)
target_compile_features(quota_fabric_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(quota_fabric_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()

# CUDA backend (optional)
if(QuotaFabric_CUDA_FOUND)
  file(GLOB QUOTAFABRIC_CUDA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/backends/cuda/*.cu")
  if(QUOTAFABRIC_CUDA_SOURCES)
    add_library(quota_fabric_cuda STATIC ${QUOTAFABRIC_CUDA_SOURCES})
    target_include_directories(quota_fabric_cuda PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(quota_fabric_cuda PUBLIC cxx_std_20)
    target_link_libraries(quota_fabric_cuda PUBLIC quota_fabric_core)
    target_link_libraries(quota_fabric_cuda PUBLIC cudart)
    add_library(QuotaFabric::cuda ALIAS quota_fabric_cuda)
    set_target_properties(quota_fabric_cuda PROPERTIES EXPORT_NAME cuda)
    target_compile_options(quota_fabric_cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/EHsc>)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install & export
# ---------------------------------------------------------------------------
set(QUOTAFABRIC_INSTALL_TARGETS quota_fabric_core)
if(TARGET quota_fabric_cuda)
  list(APPEND QUOTAFABRIC_INSTALL_TARGETS quota_fabric_cuda)
endif()

install(TARGETS ${QUOTAFABRIC_INSTALL_TARGETS}
  EXPORT QuotaFabricTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Do not use the qf.h umbrella here; install the full include tree.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/quota_fabric
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------------
# Apps
# ---------------------------------------------------------------------------
if(QUOTAFABRIC_BUILD_TOOLS)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../apps/coordinator/CMakeLists.txt)
    add_subdirectory(../apps/coordinator ${CMAKE_CURRENT_BINARY_DIR}/apps/coordinator)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../apps/agent/CMakeLists.txt)
    add_subdirectory(../apps/agent ${CMAKE_CURRENT_BINARY_DIR}/apps/agent)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../apps/ctl/CMakeLists.txt)
    add_subdirectory(../apps/ctl ${CMAKE_CURRENT_BINARY_DIR}/apps/ctl)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests / examples / benchmarks
# ---------------------------------------------------------------------------
if(QUOTAFABRIC_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../tests/CMakeLists.txt)
  add_subdirectory(../tests ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
if(QUOTAFABRIC_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/CMakeLists.txt)
  add_subdirectory(../examples ${CMAKE_CURRENT_BINARY_DIR}/examples)
endif()
if(QUOTAFABRIC_BUILD_BENCHMARKS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/CMakeLists.txt)
  add_subdirectory(../benchmarks ${CMAKE_CURRENT_BINARY_DIR}/benchmarks)
endif()
