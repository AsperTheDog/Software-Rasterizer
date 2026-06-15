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
        "\n[Engine Error]: GLM not found! \n"
        "Please pass -DGLM_INCLUDE_DIR=\"C:/path/to/folder_containing_glm\""
    )
endif()

option(ENABLE_SDL_OUTPUT "Enable optional SDL3 graphical window output" OFF)

set(SDL3_INCLUDE_DIR "" CACHE PATH "Path to the SDL3 'include' directory")
set(SDL3_LIB_DIR "" CACHE PATH "Path to the SDL3 'lib/x64' (or x86) directory containing SDL3.lib")

add_executable(SoftwareRasterizer 
    src/main.cpp 
    src/renderer.cpp
    
    src/command_buffer.hpp
    src/pipeline.hpp
    src/present.hpp
    src/renderer.hpp
)

target_include_directories(SoftwareRasterizer PRIVATE ${GLM_INCLUDE_DIR})

if(ENABLE_SDL_OUTPUT)
    if(NOT SDL3_INCLUDE_DIR OR NOT SDL3_LIB_DIR)
        message(FATAL_ERROR 
            "\n[Engine Error]: ENABLE_SDL_OUTPUT is enabled, but paths are missing!\n"
            "Please fill in both 'SDL3_INCLUDE_DIR' and 'SDL3_LIB_DIR'."
        )
    endif()

    find_library(SDL3_LIBRARY 
        NAMES SDL3 
        PATHS ${SDL3_LIB_DIR} 
        NO_DEFAULT_PATH
    )

    if(NOT SDL3_LIBRARY)
        message(FATAL_ERROR 
            "\n[Engine Error]: Could not find the SDL3 library file inside: ${SDL3_LIB_DIR}\n"
            "Make sure the folder contains 'SDL3.lib' (for MSVC) or 'libSDL3.dll.a' (for MinGW)."
        )
    endif()

    target_compile_definitions(SoftwareRasterizer PRIVATE SDL_OUTPUT)
    target_include_directories(SoftwareRasterizer PRIVATE ${SDL3_INCLUDE_DIR})
    target_link_libraries(SoftwareRasterizer PRIVATE ${SDL3_LIBRARY})
    
    message(STATUS ">>> SDL3 Manual Setup Successful! Linking to: ${SDL3_LIBRARY} <<<")
endif()