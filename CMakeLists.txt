cmake_minimum_required(VERSION 3.24)
project(IsoRecorder VERSION 0.4.0 LANGUAGES CXX)
set(APP_VERSION_SHORT "0.4")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# GStreamer (MSVC x64) - z instalacji systemowej; bez pkg-config, sciezki jawnie.
set(GST_ROOT "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}" CACHE PATH "Katalog GStreamera MSVC x86_64")
file(TO_CMAKE_PATH "${GST_ROOT}" GST_ROOT)
string(REGEX REPLACE "/$" "" GST_ROOT "${GST_ROOT}")
if(NOT EXISTS "${GST_ROOT}/include/gstreamer-1.0/gst/gst.h")
    message(FATAL_ERROR "Nie znaleziono GStreamera (pakiet development) w '${GST_ROOT}'. Ustaw GST_ROOT lub GSTREAMER_1_0_ROOT_MSVC_X86_64.")
endif()

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
qt_add_executable(IsoRecorder WIN32 ${SRC})

target_include_directories(IsoRecorder PRIVATE
    src
    "${GST_ROOT}/include/gstreamer-1.0"
    "${GST_ROOT}/include/glib-2.0"
    "${GST_ROOT}/lib/glib-2.0/include")
target_link_directories(IsoRecorder PRIVATE "${GST_ROOT}/lib")
target_link_libraries(IsoRecorder PRIVATE
    Qt6::Widgets
    gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0 gstaudio-1.0
    gobject-2.0 glib-2.0
    dwmapi)
target_compile_definitions(IsoRecorder PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    APP_VERSION="${APP_VERSION_SHORT}")
target_compile_options(IsoRecorder PRIVATE /W4 /permissive- /utf-8 /MP)
