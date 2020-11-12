cmake_minimum_required(VERSION 3.15)
project(PCMToAAC C)
include_directories("C:/mingw64/include")
link_directories("C:/mingw64/lib")
set(CMAKE_C_STANDARD 11)

add_executable(PCMToAAC main.c)
target_link_libraries(PCMToAAC avcodec avutil avformat avfilter avdevice swscale swresample postproc)