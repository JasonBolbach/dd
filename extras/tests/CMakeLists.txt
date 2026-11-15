# dd unit + stress tests (desktop). Build and run:
#     cmake -S . -B build && cmake --build build -j && build/dd_tests
# (or `ctest --test-dir build`). Same prerequisites as the desktop demo: SDL2 and a LovyanGFX checkout
# (default ../../../LovyanGFX, or -DLGFX_ROOT=...). The tests draw into memory sprites, so no window opens.
# Tip: configure with -DCMAKE_BUILD_TYPE=Release for realistic timings in the stress section.
cmake_minimum_required(VERSION 3.10)
project(dd_tests)

add_definitions(-DLGFX_SDL)

set(DD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "dd repository root")
set(LGFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../LovyanGFX" CACHE PATH "LovyanGFX checkout")
if(NOT EXISTS "${LGFX_ROOT}/src/LovyanGFX.hpp")
    message(FATAL_ERROR "LovyanGFX not found at ${LGFX_ROOT}. Clone https://github.com/lovyan03/LovyanGFX next to this repository or pass -DLGFX_ROOT=...")
endif()
set(LGFX_SRC "${LGFX_ROOT}/src")

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
# The hardware demo scene is tested headless with a fake Arduino.h (shim/); its two copies must stay identical.
target_include_directories(${PROJECT_NAME} PUBLIC ${DD_ROOT}/src ${LGFX_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/shim ${DD_ROOT}/examples/FreenoveS3Demo/src)

option(DD_DEBUG "Enable debug float formatting helpers" ON)
if(DD_DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DD_DEBUG)
endif()
set_source_files_properties(dd_tests.cpp PROPERTIES COMPILE_OPTIONS "-Wdouble-promotion")

enable_testing()
add_test(NAME dd_tests COMMAND ${PROJECT_NAME})
add_test(NAME demo_scene_copies_identical
         COMMAND ${CMAKE_COMMAND} -E compare_files ${DD_ROOT}/examples/FreenoveS3Demo/src/DemoScene.h ${DD_ROOT}/examples/PicoWDemo/src/DemoScene.h)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED SDL2)
    include_directories(${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} -lpthread ${SDL2_LIBRARIES})
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(SDL2_ROOT "C:/SDL2" CACHE PATH "SDL2 development folder (contains include/ and lib/x64/)")
    target_include_directories(${PROJECT_NAME} PUBLIC "${SDL2_ROOT}/include")
    target_link_libraries(${PROJECT_NAME} PUBLIC "${SDL2_ROOT}/lib/x64/SDL2.lib")
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
