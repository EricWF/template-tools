include(HandleLitConfig)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  @ONLY)

add_lit_testsuite(check
  "Running template-tool tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  ARGS -sv
  DEPENDS template_parser)
