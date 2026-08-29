add_executable(quota-fabric-agent main.cpp)
target_link_libraries(quota-fabric-agent PRIVATE quota_fabric_core)
target_compile_features(quota-fabric-agent PRIVATE cxx_std_20)
