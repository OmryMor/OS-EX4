cmake_minimum_required(VERSION 3.25)
project(OS_EX4)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(OS_EX4
        MemoryConstants.h
        PhysicalMemory.cpp
        PhysicalMemory.h
        SimpleTest.cpp
        VirtualMemory.cpp
        VirtualMemory.h)
