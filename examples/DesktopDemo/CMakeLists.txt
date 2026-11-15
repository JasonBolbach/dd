# dd desktop demo + stress test (SDL2 + LovyanGFX). Build:
#     cmake -S . -B build && cmake --build build -j && build/dd_demo
# Needs SDL2 (macOS: brew install sdl2 cmake) and a LovyanGFX checkout. By default LovyanGFX is looked up next to this
# repository (../../../LovyanGFX); otherwise pass  -DLGFX_ROOT=/path/to/LovyanGFX
cmake_minimum_required(VERSION 3.10)
project(dd_demo)

add_definitions(-DLGFX_SDL)

set(DD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "dd repository root")
set(LGFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../LovyanGFX" CACHE PATH "LovyanGFX checkout")
if(NOT EXISTS "${LGFX_ROOT}/src/LovyanGFX.hpp")
    message(FATAL_ERROR "LovyanGFX not found at ${LGFX_ROOT}. Clone https://github.com/lovyan03/LovyanGFX next to this repository or pass -DLGFX_ROOT=...")
endif()
set(LGFX_SRC "${LGFX_ROOT}/src")

# The demo itself, plus the LovyanGFX sources needed for its SDL platform
file(GLOB Target_Files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${LGFX_SRC}/lgfx/Fonts/efont/*.c
    ${LGFX_SRC}/lgfx/Fonts/IPA/*.c
    ${LGFX_SRC}/lgfx/Fonts/lvgl/*.c
    ${LGFX_SRC}/lgfx/utility/*.c
    ${LGFX_SRC}/lgfx/v1/*.cpp
    ${LGFX_SRC}/lgfx/v1/panel/*.cpp
    ${LGFX_SRC}/lgfx/v1/touch/*.cpp
    ${LGFX_SRC}/lgfx/v1/lv_font/*.c
)
add_executable(${PROJECT_NAME} ${Target_Files})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${DD_ROOT}/src
    ${LGFX_SRC}
)

# Float printf helpers (ToString / Print / point labels). Turn OFF for a production-style build.
option(DD_DEBUG "Enable debug float formatting helpers" ON)
if(DD_DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DD_DEBUG)
endif()

# Flag accidental float->double promotion in the demo and the dd headers it includes (not LovyanGFX)
set_source_files_properties(main.cpp sdl_main.cpp PROPERTIES COMPILE_OPTIONS "-Wdouble-promotion")

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED SDL2)
    include_directories(${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} -lpthread ${SDL2_LIBRARIES})
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    # Point these at your SDL2 development files
    set(SDL2_ROOT "C:/SDL2" CACHE PATH "SDL2 development folder (contains include/ and lib/x64/)")
    target_include_directories(${PROJECT_NAME} PUBLIC "${SDL2_ROOT}/include")
    target_link_libraries(${PROJECT_NAME} PUBLIC "${SDL2_ROOT}/lib/x64/SDL2.lib")
    # copy SDL2.dll next to the executable after every build
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_ROOT}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    find_package(SDL2 REQUIRED COMPONENTS SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
