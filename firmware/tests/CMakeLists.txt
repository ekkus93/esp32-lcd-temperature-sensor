cmake_minimum_required(VERSION 3.22)
project(monitor_host_tests LANGUAGES CXX)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_library(monitor_core ../components/monitor_types/monitor_types.cpp ../components/alert_service/alert_service.cpp ../components/sensor_registry/sensor_registry.cpp ../components/monitor_core/monitor.cpp ../components/monitor_core/history.cpp ../components/monitor_core/history_csv.cpp ../components/monitor_core/event_store.cpp ../components/monitor_core/settings_codec.cpp ../components/monitor_core/sessions.cpp ../components/monitor_core/fake_bus.cpp ../components/monitor_core/history_query.cpp ../components/monitor_core/display_model.cpp ../components/monitor_core/device_options.cpp ../components/monitor_core/captive_dns.cpp ../components/monitor_core/probe_discovery.cpp ../components/monitor_core/bounded_body_reader.cpp ../components/monitor_core/clock_diagnostics.cpp ../components/monitor_core/retirement_policy.cpp ../components/monitor_core/readable_code.cpp)
target_include_directories(monitor_core PUBLIC ../components/monitor_core/include ../components/sensor_registry/include ../components/alert_service/include ../components/monitor_types/include)
target_compile_options(monitor_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE monitor_core)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME core_behavior COMMAND core_tests)
add_executable(alert_service_tests alert_service_tests.cpp)
target_link_libraries(alert_service_tests PRIVATE monitor_core)
target_compile_options(alert_service_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME alert_service_behavior COMMAND alert_service_tests)
add_executable(monitor_sim simulator.cpp)
target_link_libraries(monitor_sim PRIVATE monitor_core)
add_executable(storage_tests storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE monitor_core Threads::Threads)
target_compile_options(storage_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME storage_recovery COMMAND storage_tests)

# Opt-in long-running profile of production segment rollover at realistic capacity.
add_executable(history_retention_profile history_retention_profile.cpp)
target_link_libraries(history_retention_profile PRIVATE monitor_core)
target_compile_options(history_retention_profile PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(settings_tests settings_tests.cpp)
target_link_libraries(settings_tests PRIVATE monitor_core)
target_compile_options(settings_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME settings_persistence COMMAND settings_tests)

add_executable(clock_diagnostics_tests clock_diagnostics_tests.cpp)
target_link_libraries(clock_diagnostics_tests PRIVATE monitor_core)
target_compile_options(clock_diagnostics_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME clock_sync_diagnostics COMMAND clock_diagnostics_tests)

add_executable(bounded_body_reader_tests bounded_body_reader_tests.cpp)
target_link_libraries(bounded_body_reader_tests PRIVATE monitor_core)
target_compile_options(bounded_body_reader_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME bounded_http_body_receipt COMMAND bounded_body_reader_tests)

# Fault-inject the production sensor SettingsService NVS load/commit boundary.
add_executable(settings_service_tests settings_service_tests.cpp ../components/settings_service/settings_service.cpp)
target_include_directories(settings_service_tests PRIVATE settings_mocks ../components/settings_service/include)
target_link_libraries(settings_service_tests PRIVATE monitor_core)
target_compile_options(settings_service_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME sensor_settings_store_failures COMMAND settings_service_tests)

add_executable(retirement_policy_tests retirement_policy_tests.cpp)
target_link_libraries(retirement_policy_tests PRIVATE monitor_core)
target_compile_options(retirement_policy_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME sensor_retirement_policy COMMAND retirement_policy_tests)

add_executable(session_tests session_tests.cpp)
target_link_libraries(session_tests PRIVATE monitor_core)
target_compile_options(session_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME session_security COMMAND session_tests)

add_executable(history_query_tests history_query_tests.cpp)
target_link_libraries(history_query_tests PRIVATE monitor_core)
target_compile_options(history_query_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME history_queries COMMAND history_query_tests)

add_executable(probe_discovery_tests probe_discovery_tests.cpp)
target_link_libraries(probe_discovery_tests PRIVATE monitor_core)
target_compile_options(probe_discovery_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME probe_discovery_selection COMMAND probe_discovery_tests)

add_executable(device_options_tests device_options_tests.cpp)
target_link_libraries(device_options_tests PRIVATE monitor_core)
target_compile_options(device_options_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME device_options COMMAND device_options_tests)

add_executable(captive_dns_tests captive_dns_tests.cpp)
target_link_libraries(captive_dns_tests PRIVATE monitor_core)
target_compile_options(captive_dns_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME captive_dns COMMAND captive_dns_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(csv_fixture csv_fixture.cpp)
target_link_libraries(csv_fixture PRIVATE monitor_core)
add_test(NAME history_csv COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/csv_test.py $<TARGET_FILE:csv_fixture>)

# The same RFC 6455 sender is compiled for ESP-IDF and fault-injected here.
add_executable(ws_frame_tests ws_frame_tests.cpp ../components/ws_live/ws_frame.cpp)
target_include_directories(ws_frame_tests PRIVATE ../components/ws_live/include)
target_compile_options(ws_frame_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME websocket_send_backpressure COMMAND ws_frame_tests)

# Exhaustively simulate interrupted TMH2 record writes at every byte boundary.
add_executable(history_power_cut_tests history_power_cut_tests.cpp)
target_link_libraries(history_power_cut_tests PRIVATE monitor_core)
target_compile_options(history_power_cut_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME history_power_cut_recovery COMMAND history_power_cut_tests)

# Exercise the production WsLive broadcast/authorization/session-close path
# with a host HTTP-server shim and real POSIX sockets; no ESP32 needed.
add_executable(ws_live_tests ws_live_tests.cpp ../components/ws_live/ws_live.cpp ../components/ws_live/ws_frame.cpp)
target_include_directories(ws_live_tests PRIVATE ws_mocks ../components/ws_live/include)
target_compile_options(ws_live_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME websocket_live_client_isolation COMMAND ws_live_tests)

# Exercise the same station NVS codec used by the firmware, without Wi-Fi hardware.
add_executable(wifi_credentials_tests wifi_credentials_tests.cpp)
target_include_directories(wifi_credentials_tests PRIVATE ../components/wifi_service/include)
target_compile_options(wifi_credentials_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME wifi_station_nvs_corruption COMMAND wifi_credentials_tests)

# Exercise production AP timeout/candidate retry policy without RF hardware.
add_executable(wifi_ap_window_tests wifi_ap_window_tests.cpp)
target_include_directories(wifi_ap_window_tests PRIVATE ../components/wifi_service/include)
target_compile_options(wifi_ap_window_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME wifi_provisioning_ap_deadline COMMAND wifi_ap_window_tests)

# Verify the exact production policy that gates OTA image acceptance.
add_executable(ota_boot_policy_tests ota_boot_policy_tests.cpp)
target_include_directories(ota_boot_policy_tests PRIVATE ../components/ota_service/include)
target_compile_options(ota_boot_policy_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ota_startup_health_policy COMMAND ota_boot_policy_tests)

# A rejected Wi-Fi scan must not leave a timer that interrupts candidate STA.
add_executable(wifi_scan_window_tests wifi_scan_window_tests.cpp)
target_include_directories(wifi_scan_window_tests PRIVATE ../components/wifi_service/include)
target_compile_options(wifi_scan_window_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME wifi_scan_candidate_isolation COMMAND wifi_scan_window_tests)

# Production read-only physical bus diagnostics use the same host-tested counters.
add_executable(sensor_diagnostics_tests sensor_diagnostics_tests.cpp)
target_include_directories(sensor_diagnostics_tests PRIVATE ../components/monitor_core/include ../components/monitor_types/include)
target_compile_options(sensor_diagnostics_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME sensor_hardware_diagnostics COMMAND sensor_diagnostics_tests)

# Every prefix of an interrupted event append must preserve committed events.
# Unknown future journal headers must never be truncated during recovery.
add_executable(event_power_cut_tests event_power_cut_tests.cpp)
target_link_libraries(event_power_cut_tests PRIVATE monitor_core)
target_compile_options(event_power_cut_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME event_journal_power_cut_recovery COMMAND event_power_cut_tests)

# Inject deterministic post-fsync, CRC-valid edits into the production
# event journal writer. GNU ld wrapping is host-only, never ESP-IDF firmware.
add_executable(event_append_integrity_tests event_append_integrity_tests.cpp)
target_link_libraries(event_append_integrity_tests PRIVATE monitor_core)
target_link_options(event_append_integrity_tests PRIVATE "LINKER:--wrap=fsync")
target_compile_options(event_append_integrity_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME event_append_post_fsync_integrity COMMAND event_append_integrity_tests)
add_executable(readable_code_tests readable_code_tests.cpp)
target_link_libraries(readable_code_tests PRIVATE monitor_core)
target_compile_options(readable_code_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME readable_code COMMAND readable_code_tests)
add_executable(aggregator_calibration_tests aggregator_calibration_tests.cpp)
target_link_libraries(aggregator_calibration_tests PRIVATE monitor_core)
target_compile_options(aggregator_calibration_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME aggregator_calibration COMMAND aggregator_calibration_tests)
