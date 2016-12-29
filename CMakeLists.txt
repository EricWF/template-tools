cmake_minimum_required(VERSION 3.4.3)
project(class_parser_proj CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

###############################################################################
# Configuration options
SET(TEMPLATE_TOOLS_USE_LIBCXX "Use libc++ when building and using the plugin" OFF)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


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

add_subdirectory(submodules/fmt)
###############################################################################
# template_parser - A Clang plugin for counting templates

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(template_parser SHARED template_parser.cpp)
install(TARGETS template_parser LIBRARY DESTINATION lib)

set_target_properties(template_parser
    PROPERTIES
      COMPILE_FLAGS "${COMPILE_FLAGS}"
      LINK_FLAGS    "${LINK_FLAGS}"
  )

target_link_libraries(template_parser
        clangBasic clangFrontend clangDriver clangLex clangParse clangAST
        clangASTMatchers clangSema clangAnalysis clangEdit clangSerialization
        LLVMSupport)

target_link_libraries(template_parser fmt)

###############################################################################
# template-count - A Clang wrapper which loads the template_parser plugin
if (APPLE)
    set(SO_SUFFIX "dylib")
else()
    set(SO_SUFFIX "so")
endif()

if (TEMPLATE_TOOLS_USE_LIBCXX)
  set(STDLIB_FLAG "-stdlib=libc++")
endif()

# Create a version which uses the libtemplate_parser.so in the build directory
set(LIBRARY_PREFIX "${CMAKE_CURRENT_BINARY_DIR}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template-count.in
               ${CMAKE_CURRENT_BINARY_DIR}/template-count)

# Create another version which will use the installed libtemplate_parser.so
set(LIBRARY_PREFIX "${CMAKE_INSTALL_PREFIX}/lib")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template-count.in
               ${CMAKE_CURRENT_BINARY_DIR}/template-count.install)

add_custom_target(template-count-bin ALL
                  COMMAND chmod +x "${CMAKE_CURRENT_BINARY_DIR}/template-count"
                  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/template-count
                  DEPENDS template_parser)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/template-count.install
        DESTINATION bin
        RENAME template-count)
