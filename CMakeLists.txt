cmake_minimum_required (VERSION 3.13)
set (BINARY "picolua")
include (pico_sdk_import.cmake)
project (picolua)
file (GLOB lua_src CONFIGURE_DEPENDS "lua/*.c")
list (REMOVE_ITEM lua_src "${CMAKE_CURRENT_SOURCE_DIR}/lua/lua.c" "${CMAKE_CURRENT_SOURCE_DIR}/lua/luac.c")
file (GLOB shell_src CONFIGURE_DEPENDS "shell/src/*.c")
file (GLOB libluapico_src CONFIGURE_DEPENDS "libluapico/src/*.c")
file (GLOB klib_src CONFIGURE_DEPENDS "klib/src/*.c")
file (GLOB bute2_src CONFIGURE_DEPENDS "bute2/src/*.c")
file (GLOB interface_src CONFIGURE_DEPENDS "interface/src/*.c")
file (GLOB storage_src CONFIGURE_DEPENDS "storage/src/*.c")
file (GLOB ymodem_src CONFIGURE_DEPENDS "ymodem/src/*.c")
pico_sdk_init()
add_executable (${BINARY} ${klib_src} ${lua_src} ${shell_src} ${interface_src} ${ymodem_src} ${interface_src} ${storage_src} ${bute2_src} ${libluapico_src})
target_link_libraries (${BINARY} PRIVATE m)
target_include_directories (
    ${BINARY} PRIVATE
    lua
    klib/include
    interface/include
    storage/include
    storage/include/storage
    shell/include
    bute2/include
    ymodem/include
    libluapico/include
    ${PROJECT_SOURCE_DIR}
)
target_link_options (
    ${BINARY} PRIVATE
    -Wl,--wrap=remove
    -Wl,--wrap=rename
    -Wl,--wrap=fopen
    -Wl,--wrap=fopen64
    -Wl,--wrap=freopen
    -Wl,--wrap=freopen64
    -Wl,--wrap=tmpfile
    -Wl,--wrap=tmpfile64
)
target_compile_options (
    ${BINARY} PRIVATE
    -Wall
    -Wextra
)
target_compile_definitions (
    ${BINARY} PRIVATE
    LUA_32BITS=1
    _GNU_SOURCE=1
    "LUA_CPATH_DEFAULT=\"\?.so\""
    "LUA_PATH_DEFAULT=\"./\?.lua\;./\?/init.lua\;/lib/\?.lua\;/lib/\?/init.lua\""
)
pico_enable_stdio_usb (${BINARY} 1)
pico_enable_stdio_uart (${BINARY} 0)
pico_add_extra_outputs (${BINARY})
if (PICO_ON_DEVICE)
    target_link_libraries (${BINARY} PRIVATE pico_stdlib hardware_flash hardware_pwm hardware_sync hardware_adc hardware_i2c)
else()
    target_link_libraries (${BINARY} PRIVATE pico_stdlib hardware_sync)
endif()
