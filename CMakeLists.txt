cmake_minimum_required(VERSION 3.10)
project(mmp_bot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Find dependencies ----
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)

# ---- Executable ----
add_executable(bot
    src/main.cpp
    src/config.cpp
    src/rest_order_executor.cpp
    src/ws_order_executor.cpp
)


# ---- Include paths ----
target_include_directories(bot PRIVATE
    src
    ${Boost_INCLUDE_DIRS}
)

# ---- Link libraries ----
target_link_libraries(bot
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)
