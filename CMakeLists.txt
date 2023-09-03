cmake_minimum_required(VERSION 3.20)
project(PSD_CPP)

add_executable(main main.cpp)
target_compile_features(main PRIVATE cxx_std_23)
set_target_properties(main PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(main PRIVATE /W4 /WX)
else()
    target_compile_options(main PRIVATE -Wall -Wno-unused-variable -Werror)
endif()
