cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()
project(halo_vita_core_test C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
get_filename_component(HALO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(HALO_CLANG NAMES clang REQUIRED)
set(PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/game")
set(GAME_OBJECTS "${PROBE_DIR}/physical_memory_map.o" "${PROBE_DIR}/xapi_memory.o" "${PROBE_DIR}/crc.o")
set(PLATFORM_OBJECTS "${PROBE_DIR}/xapi_platform.o" "${PROBE_DIR}/platform_contract.o" "${PROBE_DIR}/xinput_vita.o")
file(GLOB_RECURSE GAME_HEADERS "${HALO_ROOT}/source/*.h" "${HALO_ROOT}/port/linux/include/*.h" "${HALO_ROOT}/port/include/xdk/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
add_custom_command(OUTPUT ${GAME_OBJECTS} ${PLATFORM_OBJECTS}
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/compile_probe.py"
    --sdk "${VITASDK}" --clang "${HALO_CLANG}" --out "${PROBE_DIR}"
  DEPENDS compile_probe.py src/xapi_memory.c src/xapi_platform.c src/xinput_vita.c tests/platform_contract.c ${GAME_HEADERS}
    "${HALO_ROOT}/source/cache/physical_memory_map.c" "${HALO_ROOT}/source/memory/crc.c"
  VERBATIM)
set_source_files_properties(${GAME_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
set_source_files_properties(${PLATFORM_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_executable(halo_vita_platform_test EXCLUDE_FROM_ALL src/platform_test.c vendor/debugScreen.c ${PLATFORM_OBJECTS})
target_include_directories(halo_vita_platform_test PRIVATE vendor)
target_link_options(halo_vita_platform_test PRIVATE -Wl,--no-wchar-size-warning -Wl,--no-enum-size-warning -Wl,--gc-sections)
target_link_libraries(halo_vita_platform_test SceLibKernel_stub SceIofilemgr_stub SceDisplay_stub SceCtrl_stub SceRtc_stub m)
vita_create_self(platform-eboot.bin halo_vita_platform_test UNSAFE)
vita_create_vpk(halo-vita-platform-0.2.vpk HVIT00002 platform-eboot.bin
  VERSION "00.02" NAME "Halo Vita Platform Test"
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
set(ZLIB_SOURCES
  "${HALO_ROOT}/source/memory/zlib/adler32.c"
  "${HALO_ROOT}/source/memory/zlib/inflate.c"
  "${HALO_ROOT}/source/memory/zlib/infblock.c"
  "${HALO_ROOT}/source/memory/zlib/infcodes.c"
  "${HALO_ROOT}/source/memory/zlib/inffast.c"
  "${HALO_ROOT}/source/memory/zlib/inftrees.c"
  "${HALO_ROOT}/source/memory/zlib/infutil.c"
  "${HALO_ROOT}/source/memory/zlib/zutil.c")
add_executable(${PROJECT_NAME} src/map_test.c src/arena.c src/memory_vita.c vendor/debugScreen.c ${GAME_OBJECTS} ${ZLIB_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE include vendor "${HALO_ROOT}/source")
target_compile_options(${PROJECT_NAME} PRIVATE -O3 -Wall -Wextra -std=gnu11)
target_compile_definitions(${PROJECT_NAME} PRIVATE HALO_VITA_STANDALONE_ZLIB=1)
# Only simple integers and pointers cross this boundary; game units retain
# Xbox UTF-16 wchar_t, while SDK/newlib-facing units keep their native type.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--no-wchar-size-warning -Wl,--no-enum-size-warning -Wl,--gc-sections)
set(GPU_DEPS "${HALO_ROOT}/../gpu-hard/arm-vita-eabi")
target_include_directories(${PROJECT_NAME} PRIVATE "${GPU_DEPS}/include")
target_link_directories(${PROJECT_NAME} PRIVATE "${GPU_DEPS}/lib")
target_link_libraries(${PROJECT_NAME} vitaGL stdc++ SceLibKernel_stub SceAppMgr_stub SceAppUtil_stub mathneon c SceCommonDialog_stub m SceGxm_stub SceDisplay_stub SceSysmodule_stub vitashark SceShaccCg_stub SceKernelDmacMgr_stub toloader SceShaccCgExt taihen_stub SceCtrl_stub)
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(halo-vita-menu-bloodgulch-2.0.vpk HVIT00001 eboot.bin
  VERSION "02.00" NAME "Halo Vita BloodGulch & UI Menu"
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)

# Explicit developer target: compile every available game C unit and turn its
# remaining external references into a reproducible platform-port backlog.
add_custom_target(vita-engine-audit
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/compile_probe.py"
    --sdk "${VITASDK}" --clang "${HALO_CLANG}" --out "${CMAKE_CURRENT_BINARY_DIR}/engine-objects"
    --all --jobs 8
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/engine_link_audit.py"
    --sdk "${VITASDK}" --results "${CMAKE_CURRENT_BINARY_DIR}/engine-objects/compile-results.json"
    --out "${CMAKE_CURRENT_BINARY_DIR}/engine-audit" --jobs 8
  BYPRODUCTS
    "${CMAKE_CURRENT_BINARY_DIR}/engine-objects/compile-results.json"
    "${CMAKE_CURRENT_BINARY_DIR}/engine-audit/engine-link-audit.json"
    "${CMAKE_CURRENT_BINARY_DIR}/engine-audit/engine-link-audit.md"
  USES_TERMINAL
  VERBATIM)

add_custom_target(vita-engine-link
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/engine_link_probe.py"
    --sdk "${VITASDK}" --results "${CMAKE_CURRENT_BINARY_DIR}/engine-objects/compile-results.json"
    --out "${CMAKE_CURRENT_BINARY_DIR}/engine-link"
  DEPENDS vita-engine-audit
  USES_TERMINAL VERBATIM)
