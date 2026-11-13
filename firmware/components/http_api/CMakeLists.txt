file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/generated")
idf_component_register(SRCS "http_api.cpp" "api_responses.cpp" "idf_exchange.cpp" "service_adapters.cpp" "api_router.cpp" "routes_auth.cpp" "routes_sensors.cpp" "routes_settings.cpp" "routes_network.cpp" "live_json.cpp" "history_json.cpp" "calibration_hold_request.cpp" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "generated" REQUIRES settings_service clock_service monitor_core auth_service wifi_service history_service ota_service ws_live esp_http_server lwip esp_timer esp_psram espressif__cjson)

# Repack when a Vite build changes either asset contents or hashed filenames.
set(web_dist "${CMAKE_CURRENT_LIST_DIR}/../../../web/dist")
file(GLOB_RECURSE web_inputs CONFIGURE_DEPENDS "${web_dist}/*")
if(NOT EXISTS "${web_dist}/index.html")
    message(FATAL_ERROR "Build the web app first: npm --prefix web ci && npm --prefix web run build")
endif()
set(pack_script "${CMAKE_CURRENT_LIST_DIR}/../../../tools/pack-web.py")
set(asset_header "${CMAKE_CURRENT_LIST_DIR}/generated/web_assets.h")
set(asset_manifest "${CMAKE_CURRENT_LIST_DIR}/generated/web_assets.json")
add_custom_command(OUTPUT "${asset_header}" "${asset_manifest}"
    COMMAND ${PYTHON} "${pack_script}"
    DEPENDS ${web_inputs} "${pack_script}"
    VERBATIM)
add_custom_target(temperature_web_assets DEPENDS "${asset_header}" "${asset_manifest}")
add_dependencies(${COMPONENT_LIB} temperature_web_assets)
