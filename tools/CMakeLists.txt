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
set(LIBRARY_PREFIX "${MAIN_BUILD_DIR}/lib")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template-count.in
               ${MAIN_BUILD_DIR}/template-count)

add_custom_target(template-count-bin ALL
                  COMMAND chmod +x "${MAIN_BUILD_DIR}/template-count"
                  DEPENDS ${MAIN_BUILD_DIR}/template-count
                  DEPENDS template_parser)

# Create another version which will use the installed libtemplate_parser.so
set(LIBRARY_PREFIX "${CMAKE_INSTALL_PREFIX}/lib")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template-count.in
               ${CMAKE_CURRENT_BINARY_DIR}/template-count.install)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/template-count.install
        DESTINATION bin
        RENAME template-count)
