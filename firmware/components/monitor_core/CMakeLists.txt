idf_component_register(SRCS "monitor.cpp" "history.cpp" "history_csv.cpp" "event_store.cpp" "settings_codec.cpp" "sessions.cpp" "fake_bus.cpp" "history_query.cpp" "display_model.cpp" "device_options.cpp" "captive_dns.cpp" INCLUDE_DIRS "include" REQUIRES sensor_registry)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
