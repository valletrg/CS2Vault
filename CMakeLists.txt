cmake_minimum_required(VERSION 3.16)
project(CS2Vault LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Installation directories
include(GNUInstallDirs)
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts)
find_package(ZLIB REQUIRED)

# Enable automatic compilation of Qt resource files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/portfoliowidget.cpp
    src/settingswidget.cpp
    src/priceempireapi.cpp
    src/steamapi.cpp
    src/portfoliomanager.cpp
    src/steamcompanion.cpp
    src/storageunitwidget.cpp
    src/qrlogindialog.cpp
    src/loginwindow.cpp
    src/updatechecker.cpp
    src/itemdatabase.cpp
    src/accountmanager.cpp
    src/dashboardwidget.cpp
    src/watchlistwidget.cpp
    src/watchlistmanager.cpp
    src/tradehistory.cpp
    src/tradehistorywidget.cpp
    src/tradeswidget.cpp
)

set(HEADERS
    src/mainwindow.h
    src/dashboardwidget.h
    src/portfoliowidget.h
    src/settingswidget.h
    src/priceempireapi.h
    src/steamapi.h
    src/portfoliomanager.h
    src/steamcompanion.h
    src/storageunitwidget.h
    src/qrlogindialog.h
    src/loginwindow.h
    src/updatechecker.h
    src/accountmanager.h
    src/itemdatabase.h
    src/watchlistwidget.h
    src/watchlistmanager.h
    src/tradehistory.h
    src/tradehistorywidget.h
    src/tradeswidget.h
)

set(RESOURCES
    src/resources.qrc
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Set executable properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "CS2Vault"
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

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Svg SvgWidgets)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
    Qt6::Svg
    Qt6::SvgWidgets
    ZLIB::ZLIB
)
