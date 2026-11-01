cmake_minimum_required(VERSION 3.16)
project(qtdskbrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

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

include(GNUInstallDirs)
install(TARGETS qtdskbrowser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
