cmake_minimum_required(VERSION 3.20)
project(jslice_predictive_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(JS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(jslice_predictive_core
    "${JS_ROOT}/src/libslic3r/Predictive/CompilerPasses.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/ForwardModel.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/PredictiveCompiler.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/SupportPlanning.cpp"
)

target_include_directories(jslice_predictive_core PUBLIC "${JS_ROOT}/src")

if(MSVC)
    target_compile_options(jslice_predictive_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(jslice_predictive_core PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

add_executable(predictive_core_smoke "${JS_ROOT}/tools/predictive_core_smoke.cpp")
target_link_libraries(predictive_core_smoke PRIVATE jslice_predictive_core)

if(MSVC)
    target_compile_options(predictive_core_smoke PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(predictive_core_smoke PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

enable_testing()
add_test(NAME predictive_core_smoke COMMAND predictive_core_smoke)
