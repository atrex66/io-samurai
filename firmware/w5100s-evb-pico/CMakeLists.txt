cmake_minimum_required(VERSION 3.12)
set(PICO_SDK_PATH "$ENV{HOME}/pico-sdk")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0") # Maximális optimalizáció
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

#set(CMAKE_BUILD_TYPE Release) # Release mód

set(CMAKE_BUILD_TYPE Relase)
add_compile_options(-g -O3)

include(pico_sdk_import.cmake)

project(io-samurai C CXX ASM)

# Ellenőrizzük, hogy létezik-e a könyvtár
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ioLibrary_Driver")
    message(STATUS "Extracting ioLibrary_Driver...")
    # Szkript futtatása, ha a könyvtár nem létezik
    execute_process(
        COMMAND bash ${CMAKE_SOURCE_DIR}/ioLibraryDriver.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
    message(STATUS "IoLibrary_Driver already exists.")
endif()

# WIZnet forrásfájlok
file(GLOB_RECURSE WIZNET_SOURCES
    "ioLibrary_Driver/Internet/DHCP/*.c"
    "ioLibrary_Driver/Ethernet/*.c"
    "ioLibrary_Driver/Ethernet/W5100S/*.c"
)

# Cortex-M0+ cél explicit megadása
set(PICO_PLATFORM rp2040)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-m0plus -mthumb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-m0plus -mthumb")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=cortex-m0plus -mthumb")

pico_sdk_init()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary_Driver
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary_Driver/Ethernet
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary_Driver/Ethernet/W5100S
)

add_executable(io-samurai
    main.c
    config.c
    sh1106.c
    ${WIZNET_SOURCES}
)

target_include_directories(io-samurai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary_Driver
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_definitions(io-samurai PRIVATE
    _WIZCHIP_=W5100S
    WIZCHIP_USE_CS=1
)

target_link_libraries(io-samurai
    pico_stdlib
    hardware_spi
    pico_multicore  # Hiányzó multicore támogatás
    pico_stdio_usb
    hardware_gpio
    hardware_dma
    hardware_i2c
)

target_compile_options(io-samurai PRIVATE)
pico_add_extra_outputs(io-samurai)