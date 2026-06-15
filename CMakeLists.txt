cmake_minimum_required(VERSION 3.16)
project(SoftwareRasterizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLM_INCLUDE_DIR "" CACHE PATH "Path to the directory containing the 'glm' folder")

if(NOT GLM_INCLUDE_DIR)
    find_path(GLM_AUTO_DIR NAMES glm/glm.hpp)
    if(GLM_AUTO_DIR)
        set(GLM_INCLUDE_DIR ${GLM_AUTO_DIR})
    endif()
endif()

if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR 
        "\n[Engine Build Error]: GLM could not be found automatically!\n"
        "Please provide the path to GLM.\n"
        "-> Command line option: Pass -DGLM_INCLUDE_DIR=\"C:/path/to/folder_containing_glm\"\n"
        "-> UI option: Set the 'GLM_INCLUDE_DIR' variable in your CMake GUI or Visual Studio CMake Settings."
    )
endif()

message(STATUS "Using GLM from directory: ${GLM_INCLUDE_DIR}")

add_executable(SoftwareRasterizer 
    src/main.cpp 
    src/renderer.cpp
    
    src/command_buffer.hpp
    src/pipeline.hpp
    src/present.hpp
    src/renderer.hpp
)

target_include_directories(SoftwareRasterizer PRIVATE ${GLM_INCLUDE_DIR})