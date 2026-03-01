cmake_minimum_required(VERSION 3.10)
project(CriAudioExtractor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/extractor.cpp
)

# Executable
add_executable(CriAudioExtractor ${SOURCES})

# Include directories
target_include_directories(CriAudioExtractor PRIVATE src)

# Optional : Optimisation target for release
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "")
    if(MSVC)
        target_compile_options(CriAudioExtractor PRIVATE /O2 /fp:fast)
    else()
        target_compile_options(CriAudioExtractor PRIVATE -O3 -ffast-math -flto)
    endif()
endif()
