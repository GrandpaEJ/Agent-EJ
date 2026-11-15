cmake_minimum_required(VERSION 3.22.1)
project(ejcore C)

get_filename_component(EJ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(
    -O2
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -D_POSIX_C_SOURCE=200809L
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
)

# The non-agentic core: format parsers, APK pipeline and crypto only.
# No tools/, net/ or core/ (those pull in cJSON, sockets and the agent loop).
file(GLOB EJ_CORE_SRC
    ${EJ_ROOT}/src/formats/zip/*.c
    ${EJ_ROOT}/src/android/axml/*.c
    ${EJ_ROOT}/src/android/arsc/*.c
    ${EJ_ROOT}/src/android/dex/*.c
    ${EJ_ROOT}/src/android/smali/*.c
    ${EJ_ROOT}/src/android/apk/*.c
    ${EJ_ROOT}/src/crypto/*.c
)

file(GLOB EJ_JNI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_library(ejcore SHARED ${EJ_CORE_SRC} ${EJ_JNI_SRC})

target_include_directories(ejcore PRIVATE
    ${EJ_ROOT}/include
    ${EJ_ROOT}/src/android/smali
    ${EJ_ROOT}/src/android/arsc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ejcore android log)
set_target_properties(ejcore PROPERTIES LINK_FLAGS
    "-Wl,-z,max-page-size=16384 -Wl,--gc-sections -Wl,--exclude-libs,ALL")
