cmake_minimum_required(VERSION 3.13)

add_executable(minibrowser
    main.cpp
)


target_include_directories(minibrowser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(minibrowser PRIVATE QT_NO_KEYWORDS)
target_link_libraries(minibrowser
    PRIVATE
        dtkwebkit
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Widgets
        Dtk6::Core
        Dtk6::Widget
)
