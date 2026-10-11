cmake_minimum_required(VERSION 3.15)
project(ExtraStationsSA CXX)

# GTA SA is a 32-bit game: configure with  -A Win32  (MSVC) or an i686 MinGW toolchain.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ExtraStations.SA must be built as 32-bit (use: cmake -B build -A Win32)")
endif()

set(CMAKE_CXX_STANDARD 17)
# static runtime: the plugin must not need the Visual C++ redistributable on the player's PC (same as /MT in tools/build.ps1)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_library(ExtraStationsSA SHARED src/ExtraStations.cpp)
set_target_properties(ExtraStationsSA PROPERTIES OUTPUT_NAME "ExtraStations.SA" SUFFIX ".asi" PREFIX "")
