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
