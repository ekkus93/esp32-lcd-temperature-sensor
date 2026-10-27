cmake_minimum_required(VERSION 3.22)
project(monitor_display_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
enable_testing()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(lvgl_source "${CMAKE_CURRENT_LIST_DIR}/../managed_components/lvgl__lvgl")
if(NOT EXISTS "${lvgl_source}/CMakeLists.txt")
    message(FATAL_ERROR "Resolve pinned firmware components with idf.py reconfigure first")
endif()
add_subdirectory("${lvgl_source}" lvgl)
find_package(PNG REQUIRED)
add_executable(display_tests display_tests.cpp
    ../components/display_service/display_view.cpp
    ../components/monitor_types/monitor_types.cpp
    ../components/alert_service/alert_service.cpp
    ../components/sensor_registry/sensor_registry.cpp
    ../components/monitor_core/monitor.cpp
    ../components/monitor_core/display_model.cpp
    ../components/monitor_core/fake_bus.cpp)
target_include_directories(display_tests PRIVATE ../components/display_service/include ../components/monitor_core/include ../components/sensor_registry/include ../components/alert_service/include ../components/monitor_types/include)
target_link_libraries(display_tests PRIVATE lvgl::lvgl PNG::PNG)
target_compile_options(display_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME display_render COMMAND display_tests "${CMAKE_CURRENT_BINARY_DIR}/screenshots")
