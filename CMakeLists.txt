cmake_minimum_required(VERSION 3.16)
project(qtdskbrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

if (CMAKE_CROSSCOMPILING)
    # A win64_mingw Qt6 package only ships Windows .exe moc/rcc/uic, which
    # can't run on this (Linux) build host. Point AUTOMOC/AUTORCC/AUTOUIC at
    # a native Qt6 install's tools instead (QT_HOST_PATH, set by the
    # mingw-w64 toolchain file).
    foreach(tool moc rcc uic)
        find_program(QT_HOST_${tool}_EXECUTABLE ${tool}
            PATHS "${QT_HOST_PATH}/lib/qt6/libexec" "${QT_HOST_PATH}/libexec/qt6" "${QT_HOST_PATH}/bin"
            NO_DEFAULT_PATH REQUIRED)
        set_target_properties(Qt6::${tool} PROPERTIES IMPORTED_LOCATION "${QT_HOST_${tool}_EXECUTABLE}")
    endforeach()
endif()

add_executable(qtdskbrowser
    main.cpp
    dskimage.cpp
    dskimage.h
    detokenizer.cpp
    detokenizer.h
    basicviewer.cpp
    basicviewer.h
    cpcglyphs.h
)

target_link_libraries(qtdskbrowser PRIVATE Qt6::Widgets)

if (WIN32)
    # Link as a GUI app, not a console app, so no console window pops up
    # behind the window on Windows.
    set_target_properties(qtdskbrowser PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(GNUInstallDirs)
install(TARGETS qtdskbrowser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
