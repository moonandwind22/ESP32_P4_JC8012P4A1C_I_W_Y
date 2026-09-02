# LondonBrief_P4 — ESP32-P4 firmware for the JC8012P4A1C_I_W_Y board.
#
# ESP-IDF (v5.5.x) project that uses arduino-esp32 3.3.x as a component.
#
# Build:      idf.py build
# Variants:   idf.py -DLONDONBRIEF_WIFI_DEBUG=1 build   (Wi-Fi/hosted diagnostics)
#             idf.py -DLONDONBRIEF_SERVICE_DEBUG=1 build (service logging)
#
# The arduino-esp32 core is a git submodule:
#   git clone --recurse-submodules <this repo>
#   git submodule update --init --recursive

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Board-specific workaround carried over from the PlatformIO build
# (scripts/patch_hosted_framework.py): skip the hostedHasUpdate() call in the
# Arduino core because its version RPC can wedge against this board's C6
# co-processor firmware. The patch is idempotent and re-applies automatically
# after the components/arduino submodule is re-cloned.
# Disable with: idf.py -DLONDONBRIEF_PATCH_HOSTED=0 build
if(NOT DEFINED LONDONBRIEF_PATCH_HOSTED)
    set(LONDONBRIEF_PATCH_HOSTED 1)
endif()

if(LONDONBRIEF_PATCH_HOSTED)
    set(londonbrief_hosted_file "${CMAKE_CURRENT_LIST_DIR}/components/arduino/cores/esp32/esp32-hal-hosted.c")
    if(EXISTS "${londonbrief_hosted_file}")
        file(READ "${londonbrief_hosted_file}" londonbrief_hosted_src)
        if(NOT londonbrief_hosted_src MATCHES "LondonBrief patch: skip hostedHasUpdate")
            string(REPLACE
                "  hosted_initialized = true;\n  hostedHasUpdate();"
                "  hosted_initialized = true;\n  /* LondonBrief patch: skip hostedHasUpdate() because version RPC wedges on this board. */"
                londonbrief_hosted_patched "${londonbrief_hosted_src}")
            if(NOT londonbrief_hosted_patched STREQUAL londonbrief_hosted_src)
                file(WRITE "${londonbrief_hosted_file}" "${londonbrief_hosted_patched}")
                message(STATUS "LondonBrief: patched esp32-hal-hosted.c (hostedHasUpdate() skipped)")
            else()
                message(WARNING "LondonBrief: could not locate hostedHasUpdate() call in esp32-hal-hosted.c; patch not applied")
            endif()
        endif()
    else()
        message(WARNING "LondonBrief: ${londonbrief_hosted_file} not found; run 'git submodule update --init --recursive'")
    endif()
endif()

# Make the project lv_conf.h visible to every component that compiles LVGL
# (the lvgl managed component builds with LV_CONF_INCLUDE_SIMPLE).
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/main/include" APPEND)

project(LondonBrief_P4)
