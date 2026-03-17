cmake_minimum_required(VERSION 3.16)
project(CS2Trader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Installation directories
include(GNUInstallDirs)
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts)

# Enable automatic compilation of Qt resource files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SOURCES
    main.cpp
    mainwindow.cpp
    calculatorwidget.cpp
    portfoliowidget.cpp
    settingswidget.cpp
    priceempireapi.cpp
    steamapi.cpp
    portfoliomanager.cpp
    steamcompanion.cpp
    qrlogindialog.cpp
)

set(HEADERS
    mainwindow.h
    calculatorwidget.h
    portfoliowidget.h
    settingswidget.h
    priceempireapi.h
    steamapi.h
    portfoliomanager.h
    steamcompanion.h
    qrlogindialog.h
)

# Resource files
set(RESOURCES
    resources.qrc
)

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Set executable properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "CS2Trader"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Installation rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install desktop file (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cs2-trader.desktop")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cs2-trader.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
    )
endif()

# Uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
