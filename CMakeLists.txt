cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# STM32H743 CPU flags (Cortex-M7 with FPv5-D16 FPU)
set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MCU_FLAGS} -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H743XX_FLASH.ld -Wl,--gc-sections -Wl,--print-memory-usage")

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME LVGL_project)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# ===================== LVGL =====================
# 收集 LVGL 核心源码。只收 .c：.cpp 是 thorvg/gltf 等可选功能，默认关闭不编译
file(GLOB_RECURSE LVGL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})

# LVGL 的头文件搜索路径（lvgl.h 在 Lvgl/，内部头在 Lvgl/src/）
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/Lvgl/src
)

# 两个宏：一个让 LVGL 用 #include "lv_conf.h" 找到配置，
# 一个让 LVGL 用 #include "lvgl.h" 这种简洁写法
target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)


# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/ui.c
     Lvgl/porting/lv_port_lcd_stm32.c
    Lvgl/photo/photo_01.c
    Lvgl/photo/photo_02.c
    Lvgl/photo/photo_03.c
    Lvgl/porting/lv_port_indev.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Post-build: generate .bin and .hex files
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMENT "Generating .bin and .hex files..."
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    lvgl
)
