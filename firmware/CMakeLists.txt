# Peristaltic condensate pump firmware (design.md §6).
#
#   board esp32c3_supermini : the real pump (TMC2209 step/dir, GPIO sensors)
#   board native_sim        : simulated tank + motor, same control code
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peristaltic_pump)

target_sources(app PRIVATE
    src/main.c
    src/control.c
    src/pump.c
    src/level.c
    src/params.c
    src/app_shell.c
)

if(CONFIG_PUMP_SIM)
    target_sources(app PRIVATE src/sim.c)
else()
    target_sources(app PRIVATE
        src/motor_stepper.c
        src/level_gpio.c
    )
endif()
