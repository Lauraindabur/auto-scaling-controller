cmake_minimum_required(VERSION 3.13)
project(ControllerTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

foreach(t test_moving_average test_cooldown_manager test_config)
    add_executable(${t} ${t}.cpp)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# End-to-end simulado: DecisionEngine real con fuente de metricas y actuador falsos (sin AWS).
add_executable(test_e2e_decision_engine
    test_e2e_decision_engine.cpp
    ../src/DecisionEngine.cpp
    ../src/Logger.cpp)
target_include_directories(test_e2e_decision_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_e2e_decision_engine COMMAND test_e2e_decision_engine)
