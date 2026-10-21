cmake_minimum_required(VERSION 3.22)

project(home-gateway-orangepi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(src
    src/main.cpp
    src/config.cpp
    src/mqtt_process.cpp
    src/device_message.cpp
    src/data_handlers.cpp
    src/handle_data.cpp
    src/sqlite_resp.cpp)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)
find_library(MOSQUITTO_LIBRARY NAMES mosquitto REQUIRED)

add_executable(${PROJECT_NAME} ${src})

target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${MOSQUITTO_LIBRARY}
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    Threads::Threads)
