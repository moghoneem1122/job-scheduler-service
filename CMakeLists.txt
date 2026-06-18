cmake_minimum_required(VERSION 3.16)

project(JobScheduler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    external/Crow/include
    external/asio/asio/include
)

add_definitions(-DASIO_STANDALONE)

add_executable(JobScheduler
    src/main.cpp
)
find_package(SQLite3 REQUIRED)

target_link_libraries(JobScheduler sqlite3)