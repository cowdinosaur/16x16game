# The following lines of boilerplate have to be in your project's
# CMakeLists.txt in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add the VL53L0X component directory
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(16x16game)