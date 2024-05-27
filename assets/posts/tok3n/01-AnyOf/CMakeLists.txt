cmake_minimum_required (VERSION 3.26)

set(_TARGET k3_blog_post_AnyOf)

file(GLOB_RECURSE list_of_files
    LIST_DIRECTORIES false
    "include/*.h"
)

add_executable(${_TARGET}
    ${list_of_files}
    "main.cpp"
)

target_include_directories(${_TARGET} PUBLIC include)

set_target_properties(${_TARGET}
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)
target_compile_features(${_TARGET} INTERFACE cxx_std_20)
