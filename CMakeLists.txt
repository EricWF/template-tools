cmake_minimum_required(VERSION 3.4.3)
project(class_parser_proj CXX)

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

###############################################################################
# Configuration options
SET(TEMPLATE_TOOLS_USE_LIBCXX "Use libc++ when building and using the plugin" ON)
SET(TEMPLATE_TOOLS_ENABLE_ASSERTIONS "Enable assertions in the plugin" ON)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAIN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TOOLS_DIR "${MAIN_SOURCE_DIR}/tools")

###############################################################################
# Setup compile flags
include(HandleFlags)
set(COMPILE_FLAGS "-fno-rtti -std=c++14")
set(LINK_FLAGS "-Wl,-z,defs")

# Add flags needed by deps/fmt to CMAKE_CXX_FLAGS
if (TEMPLATE_TOOLS_USE_LIBCXX)
    list(APPEND CMAKE_CXX_FLAGS -stdlib=libc++)
endif()
list(APPEND CMAKE_CXX_FLAGS -fPIC)

# Ensure assertions are en
if (TEMPLATE_TOOLS_ENABLE_ASSERTIONS)
    add_definitions( -D_DEBUG )
    # On non-Debug builds cmake automatically defines NDEBUG, so we
    # explicitly undefine it:
    if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
        add_definitions( -UNDEBUG )
        # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
        foreach (flags_var_to_scrub
            CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS_MINSIZEREL
            CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_MINSIZEREL)
        string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
            "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
        endforeach()
    endif()
endif(TEMPLATE_TOOLS_ENABLE_ASSERTIONS)

split_list(CMAKE_CXX_FLAGS)
split_list(COMPILE_FLAGS)
split_list(LINK_FLAGS)
###############################################################################
# Configure deps

# Find the LLVM and Clang CMake modules and include them.
include(HandleLLVMConfig)
configure_out_of_tree_llvm()

include(FindPythonInterp)
if (NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "Failed to find python interpreter.")
endif()

add_subdirectory(deps/fmt)

###############################################################################
# Configure project
add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(test)
