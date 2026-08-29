add_executable(quota-fabricctl main.cpp)
target_link_libraries(quota-fabricctl PRIVATE quota_fabric_core)
target_compile_features(quota-fabricctl PRIVATE cxx_std_20)
