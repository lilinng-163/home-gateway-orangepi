cmake_minimum_required(VERSION 4.00)

project(prj)
enable_language(C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(${CMAKE_PROJECT_NAME})

set(src src/main.cpp src/src/sqlite_resp.cpp src/src/mqtt_process.cpp src/src/config.cpp)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${src})

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC src/inc)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE mosquitto sqlite3)
