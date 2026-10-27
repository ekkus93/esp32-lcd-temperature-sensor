idf_component_register(SRCS "alert_service.cpp" INCLUDE_DIRS "include" REQUIRES monitor_types)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
