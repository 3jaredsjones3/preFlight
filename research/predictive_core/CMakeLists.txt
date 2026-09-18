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
    "${JS_ROOT}/src/libslic3r/Predictive/LegacySnapshot.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/LegacyRoleMapping.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/ArtifactJson.cpp"
    "${JS_ROOT}/src/libslic3r/Predictive/CanonicalGCodeComparator.cpp"
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

foreach(tool predictive_adapter_tests predictive_compare_gcode)
    add_executable(${tool} "${JS_ROOT}/tools/${tool}.cpp")
    target_link_libraries(${tool} PRIVATE jslice_predictive_core)
    if(MSVC)
        target_compile_options(${tool} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${tool} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endforeach()
add_test(NAME predictive_adapter_tests COMMAND predictive_adapter_tests "${JS_ROOT}/tests/predictive/golden")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME predictive_golden_invariants COMMAND "${Python3_EXECUTABLE}" "${JS_ROOT}/tools/predictive_golden.py" validate "${JS_ROOT}/tests/predictive/golden")
add_test(NAME predictive_job_inputs COMMAND "${Python3_EXECUTABLE}" "${JS_ROOT}/tools/predictive_golden.py" validate-jobs "${JS_ROOT}/tests/predictive/jobs/manifest.json")
add_test(NAME predictive_golden_runner COMMAND "${Python3_EXECUTABLE}" "${JS_ROOT}/tools/predictive_golden_tests.py" "$<TARGET_FILE:predictive_compare_gcode>")
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import jsonschema" RESULT_VARIABLE JS_HAS_JSONSCHEMA OUTPUT_QUIET ERROR_QUIET)
if(JS_HAS_JSONSCHEMA EQUAL 0)
    add_test(NAME predictive_golden_schema COMMAND "${Python3_EXECUTABLE}" "${JS_ROOT}/tools/predictive_golden.py" validate "${JS_ROOT}/tests/predictive/golden" --schema "${JS_ROOT}/docs/predictive-slicer/schema/legacy-analysis.schema.json")
else()
    message(STATUS "jsonschema unavailable: structural invariant checks remain enabled")
endif()
