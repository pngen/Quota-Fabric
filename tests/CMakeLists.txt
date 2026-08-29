# Quota Fabric tests. Each category is its own executable so failures are
# isolated and the multiprocess/CUDA proofs remain independently runnable.

set(QF_TEST_CORE quota_fabric_core)

function(qf_add_test TARGET)
  set(SRC ${ARGN})
  if(MSVC)
    add_executable(${TARGET} ${SRC})
  else()
    add_executable(${TARGET} ${SRC})
  endif()
  target_link_libraries(${TARGET} PRIVATE quota_fabric_core)
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(${TARGET} PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
  endif()
  add_test(NAME ${TARGET} COMMAND ${TARGET})
endfunction()

# --- unit ---
file(GLOB QF_UNIT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
if(QF_UNIT_SRCS)
  qf_add_test(quota_fabric_unit ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_UNIT_SRCS})
endif()

# --- property ---
file(GLOB QF_PROP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/property/*.cpp)
if(QF_PROP_SRCS)
  qf_add_test(quota_fabric_property ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_PROP_SRCS})
endif()

# --- concurrency ---
file(GLOB QF_CONC_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/concurrency/*.cpp)
if(QF_CONC_SRCS)
  qf_add_test(quota_fabric_concurrency ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_CONC_SRCS})
endif()

# --- adversarial ---
file(GLOB QF_ADV_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/adversarial/*.cpp)
if(QF_ADV_SRCS)
  qf_add_test(quota_fabric_adversarial ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_ADV_SRCS})
endif()

# --- persistence ---
file(GLOB QF_PERSIST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/persistence/*.cpp)
if(QF_PERSIST_SRCS)
  qf_add_test(quota_fabric_persistence ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_PERSIST_SRCS})
endif()

# --- protocol ---
file(GLOB QF_PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp)
if(QF_PROTO_SRCS)
  qf_add_test(quota_fabric_protocol ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_PROTO_SRCS})
endif()

# --- multiprocess (real OS processes) ---
if(QUOTAFABRIC_BUILD_TOOLS)
  file(GLOB QF_MP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/multiprocess/*.cpp)
  if(QF_MP_SRCS)
    qf_add_test(quota_fabric_multiprocess ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_MP_SRCS})
    target_compile_definitions(quota_fabric_multiprocess PRIVATE QF_HAVE_TOOLS=1)
  endif()
endif()

# --- CUDA (real hardware) ---
if(QuotaFabric_CUDA_FOUND)
  file(GLOB QF_CUDA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/cuda/*.cpp)
  if(QF_CUDA_SRCS)
    qf_add_test(quota_fabric_cuda_test ${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp ${QF_CUDA_SRCS})
    target_link_libraries(quota_fabric_cuda_test PRIVATE quota_fabric_cuda)
    target_compile_definitions(quota_fabric_cuda_test PRIVATE QF_HAVE_CUDA=1)
  endif()
endif()
