cmake_minimum_required(VERSION 3.13)

# Event translator unit test
add_executable(test_event_translator
    test_event_translator.cpp
)
target_include_directories(test_event_translator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_event_translator PRIVATE QT_NO_KEYWORDS)
target_link_libraries(test_event_translator
    PRIVATE
        dtkwebkit
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
)

# Scheme handler unit test
add_executable(test_scheme_handler
    test_scheme_handler.cpp
)
target_include_directories(test_scheme_handler PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scheme_handler PRIVATE QT_NO_KEYWORDS)
target_link_libraries(test_scheme_handler
    PRIVATE
        dtkwebkit
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
)

include(CTest)
add_test(NAME test_event_translator COMMAND test_event_translator)
add_test(NAME test_scheme_handler COMMAND test_scheme_handler)
