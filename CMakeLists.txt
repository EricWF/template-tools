cmake_minimum_required(VERSION 3.4.3)
project(class_parser_proj CXX)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Find the LLVM and Clang CMake modules and include them.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(HandleLLVMConfig)
configure_out_of_tree_llvm()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(class_parser SHARED class_parser.cpp)
install(TARGETS class_parser LIBRARY DESTINATION lib)

set_target_properties(class_parser
    PROPERTIES
      COMPILE_FLAGS "-O2 -fno-rtti -fPIC -std=c++14 -stdlib=libc++"
      LINK_FLAGS    "-stdlib=libc++"
  )

target_link_libraries(class_parser
        clangBasic clangFrontend clangDriver clangLex clangParse clangAST
        clangASTMatchers clangSema clangAnalysis clangEdit clangSerialization
        LLVMSupport)

find_package(fmt REQUIRED)
target_link_libraries(class_parser fmt)

if (APPLE)
    set(SO_SUFFIX "dylib")
else()
    set(SO_SUFFIX "so")
endif()

set(LIBRARY_PREFIX "${CMAKE_CURRENT_BINARY_DIR}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/clangv8.in
               ${CMAKE_CURRENT_BINARY_DIR}/clangv8)

set(LIBRARY_PREFIX "${CMAKE_INSTALL_PREFIX}/lib")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/clangv8.in
               ${CMAKE_CURRENT_BINARY_DIR}/clangv8.install)

add_custom_target(clangv8-bin ALL
                  COMMAND chmod +x "${CMAKE_CURRENT_BINARY_DIR}/clangv8"
                  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clangv8
                  DEPENDS class_parser)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/clangv8.install
        DESTINATION bin
        RENAME clangv8)
