cmake_minimum_required(VERSION 3.4.3)
project(class_parser_proj CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

###############################################################################
# Configuration options
SET(TEMPLATE_TOOLS_USE_LIBCXX "Use libc++ when building and using the plugin" OFF)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAIN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TOOLS_DIR "${MAIN_SOURCE_DIR}/tools")

###############################################################################
# Setup compile flags
include(HandleFlags)
set(COMPILE_FLAGS "-fno-rtti -fPIC -std=c++14")
set(LINK_FLAGS "")

if (TEMPLATE_TOOLS_USE_LIBCXX)
    list(APPEND CMAKE_CXX_FLAGS -stdlib=libc++)
    list(APPEND COMPILE_FLAGS -stdlib=libc++)
    list(APPEND LINK_FLAGS -stdlib=libc++)
endif()

split_list(COMPILE_FLAGS)
split_list(LINK_FLAGS)
###############################################################################
# Configure deps

# Find the LLVM and Clang CMake modules and include them.
include(HandleLLVMConfig)
configure_out_of_tree_llvm()

add_subdirectory(deps/fmt)

###############################################################################
# Configure project
add_subdirectory(lib)
add_subdirectory(tools)

