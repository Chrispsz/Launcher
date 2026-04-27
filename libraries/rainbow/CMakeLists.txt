cmake_minimum_required(VERSION 3.1)
project(rainbow)

find_package(Qt5Core REQUIRED QUIET)
find_package(Qt5Gui REQUIRED QUIET)

set(RAINBOW_SOURCES
src/rainbow.cpp
)

# LAUNCHERMC: Use RAINBOW_STATIC since this is a static library.
# Without this, consumers on Windows get Q_DECL_IMPORT (dllimport) symbols
# that the linker cannot resolve because there is no DLL.
add_library(Launcher_rainbow STATIC ${RAINBOW_SOURCES})
target_compile_definitions(Launcher_rainbow PUBLIC RAINBOW_STATIC)
target_include_directories(Launcher_rainbow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(Launcher_rainbow Qt5::Core Qt5::Gui)

# Install it
install(
    TARGETS Launcher_rainbow
    RUNTIME DESTINATION ${LIBRARY_DEST_DIR}
    LIBRARY DESTINATION ${LIBRARY_DEST_DIR}
)
