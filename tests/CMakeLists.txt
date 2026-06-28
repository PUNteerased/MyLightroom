cmake_minimum_required(VERSION 3.24)

add_executable(mylr_tests
    test_develop_pipeline.cpp
    test_classical_matcher.cpp
    test_sidecar_io.cpp
    test_lut_engine.cpp
    test_xmp_parser.cpp
    test_lut_3dl.cpp
)

target_link_libraries(mylr_tests PRIVATE
    mylr_core
    mylr_render
    mylr_lut
    mylr_ai
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

include(GoogleTest OPTIONAL)
add_test(NAME DevelopPipeline COMMAND mylr_tests)
