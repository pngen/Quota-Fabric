add_executable(quota-fabric-coordinator main.cpp)
target_link_libraries(quota-fabric-coordinator PRIVATE quota_fabric_core)
target_compile_features(quota-fabric-coordinator PRIVATE cxx_std_20)
