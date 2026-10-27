idf_component_register(SRCS "sensor_registry.cpp" INCLUDE_DIRS "include" REQUIRES alert_service)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
