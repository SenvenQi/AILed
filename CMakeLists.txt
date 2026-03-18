cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ailed)

spiffs_create_partition_image(storage spiffs_data FLASH_IN_PROJECT)
