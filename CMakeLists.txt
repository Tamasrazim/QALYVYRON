cmake_minimum_required(VERSION 3.21)

project(QALYVYRON
    VERSION 0.1.0
    DESCRIPTION "A visual-first 3D creation and rendering studio"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Quick3D
    QuickDialogs2
    Test
)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(QALYVYRON
    src/main.cpp
    src/projectio.cpp
    include/projectio.h
)

target_include_directories(QALYVYRON PRIVATE include)

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_qml_module(QALYVYRON
    URI Qalyvyron
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    SOURCES
        include/editablegeometry.h
        src/editablegeometry.cpp
)

target_link_libraries(QALYVYRON PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    Qt6::QuickDialogs2
)

if(WIN32)
    set_target_properties(QALYVYRON PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS QALYVYRON
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

if(BUILD_TESTING)
    qt_add_executable(qalyvyron_projectio_tests
        tests/test_projectio.cpp
        src/projectio.cpp
        include/projectio.h
    )
    target_include_directories(qalyvyron_projectio_tests PRIVATE include)
    target_link_libraries(qalyvyron_projectio_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ProjectIOTests COMMAND qalyvyron_projectio_tests)

    qt_add_executable(qalyvyron_geometry_tests
        tests/test_editablegeometry.cpp
        src/editablegeometry.cpp
        include/editablegeometry.h
    )
    target_include_directories(qalyvyron_geometry_tests PRIVATE include)
    target_link_libraries(qalyvyron_geometry_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Quick3D
        Qt6::Test
    )
    add_test(NAME EditableGeometryTests COMMAND qalyvyron_geometry_tests)
endif()
