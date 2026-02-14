# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(BOARD esp32c6_devkitc/esp32c6/hpcore)

find_package(Zephyr)
project(iot-environmental-monitor)

target_sources(app PRIVATE src/main.c)
