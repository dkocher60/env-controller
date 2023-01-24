cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main dht esp-tahu mqtt-app temp-controller wifi)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(env-controller)