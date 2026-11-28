# Peristaltic condensate pump firmware (design.md §6).
#
#   board esp32c3_supermini : the real pump (TMC2209 step/dir, GPIO sensors, Wi-Fi)
#   board native_sim        : simulated tank + motor, host network, same code
cmake_minimum_required(VERSION 3.20.0)

# Wi-Fi provisioning: the captive-portal module on the device, a stub on the
# host (whose network is reached through offloaded sockets).
if(BOARD MATCHES "native_sim")
    list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/modules/wifi_prov_sim)
else()
    list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/modules/wifi_prov)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peristaltic_pump)

target_sources(app PRIVATE
    src/main.c
    src/control.c
    src/pump.c
    src/level.c
    src/params.c
    src/app_shell.c
    src/cmnd.c
    src/report.c
    src/netcfg.c
    src/pumpnet.c
    src/timesync.c
)

if(CONFIG_PUMP_SIM)
    target_sources(app PRIVATE src/sim.c)
else()
    target_sources(app PRIVATE
        src/motor_stepper.c
        src/level_gpio.c
    )
endif()
