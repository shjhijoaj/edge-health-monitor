cmake_minimum_required(VERSION 3.16)
project(edge_health_monitor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(edge_firmware_core STATIC
    firmware/src/eh_protocol.c
    firmware/src/eh_measurement.c
)
target_include_directories(edge_firmware_core PUBLIC firmware/include)

add_library(edge_gateway_core STATIC gateway/src/gateway.cpp)
target_include_directories(edge_gateway_core PUBLIC gateway/include firmware/include)
target_link_libraries(edge_gateway_core PUBLIC edge_firmware_core)

add_executable(edge_health_sim gateway/src/main.cpp)
target_link_libraries(edge_health_sim PRIVATE edge_gateway_core)

add_executable(test_protocol tests/test_protocol.c)
target_link_libraries(test_protocol PRIVATE edge_firmware_core)
target_include_directories(test_protocol PRIVATE firmware/include)

add_executable(test_gateway tests/test_gateway.cpp)
target_link_libraries(test_gateway PRIVATE edge_gateway_core)
target_include_directories(test_gateway PRIVATE gateway/include firmware/include)

if(MSVC)
    target_compile_options(edge_firmware_core PRIVATE /W4)
    target_compile_options(edge_gateway_core PRIVATE /W4)
    target_compile_options(test_protocol PRIVATE /W4)
    target_compile_options(test_gateway PRIVATE /W4)
else()
    target_compile_options(edge_firmware_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(edge_gateway_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_protocol PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test_gateway PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME protocol_roundtrip COMMAND test_protocol)
add_test(NAME gateway_rules COMMAND test_gateway)
