macro(find_llvm_parts)
# Rely on llvm-config.
  set(CONFIG_OUTPUT)
  if(NOT LLVM_CONFIG_PATH)
    find_program(LLVM_CONFIG_PATH "llvm-config")
  endif()
  if(LLVM_CONFIG_PATH)
    message(STATUS "Found LLVM_CONFIG_PATH as ${LLVM_CONFIG_PATH}")
    set(LIBCXX_USING_INSTALLED_LLVM 1)
    set(CONFIG_COMMAND ${LLVM_CONFIG_PATH}
      "--includedir"
      "--prefix"
      "--src-root")
    execute_process(
      COMMAND ${CONFIG_COMMAND}
      RESULT_VARIABLE HAD_ERROR
      OUTPUT_VARIABLE CONFIG_OUTPUT
    )
    if(NOT HAD_ERROR)
      string(REGEX REPLACE
        "[ \t]*[\r\n]+[ \t]*" ";"
        CONFIG_OUTPUT ${CONFIG_OUTPUT})
    else()
      string(REPLACE ";" " " CONFIG_COMMAND_STR "${CONFIG_COMMAND}")
      message(STATUS "${CONFIG_COMMAND_STR}")
      message(FATAL_ERROR "llvm-config failed with status ${HAD_ERROR}")
    endif()

    list(GET CONFIG_OUTPUT 0 INCLUDE_DIR)
    list(GET CONFIG_OUTPUT 1 LLVM_OBJ_ROOT)
    list(GET CONFIG_OUTPUT 2 MAIN_SRC_DIR)

    set(LLVM_INCLUDE_DIR ${INCLUDE_DIR} CACHE PATH "Path to llvm/include")
    set(LLVM_BINARY_DIR ${LLVM_OBJ_ROOT} CACHE PATH "Path to LLVM build tree")
    set(LLVM_MAIN_SRC_DIR ${MAIN_SRC_DIR} CACHE PATH "Path to LLVM source tree")
    set(LLVM_LIBRARY_DIR "${LLVM_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}")
    set(LLVM_CMAKE_PATH  "${LLVM_LIBRARY_DIR}/cmake/llvm")
    set(CLANG_CMAKE_PATH "${LLVM_LIBRARY_DIR}/cmake/clang")
  else()
    set(LLVM_FOUND OFF)
    message(WARNING "UNSUPPORTED LIBCXX CONFIGURATION DETECTED: "
                    "llvm-config not found and LLVM_PATH not defined.\n"
                    "Reconfigure with -DLLVM_CONFIG_PATH=path/to/llvm-config "
                    "or -DLLVM_PATH=path/to/llvm-source-root.")
    return()
  endif()

  if (EXISTS "${LLVM_CMAKE_PATH}")
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_PATH}")
  elseif (EXISTS "${LLVM_MAIN_SRC_DIR}/cmake/modules")
    list(APPEND CMAKE_MODULE_PATH "${LLVM_MAIN_SRC_DIR}/cmake/modules")
  else()
    set(LLVM_FOUND OFF)
    message(WARNING "Neither ${LLVM_CMAKE_PATH} nor ${LLVM_MAIN_SRC_DIR}/cmake/modules found")
    return()
  endif()
  if (EXISTS "${CLANG_CMAKE_PATH}")
    list(APPEND CMAKE_MODULE_PATH "${CLANG_CMAKE_PATH}")
  else()
    set(LLVM_FOUND OFF)
    return()
  endif()
  set(LLVM_FOUND ON)
endmacro(find_llvm_parts)

macro(configure_out_of_tree_llvm)
  find_llvm_parts()
  if (NOT LLVM_FOUND)
      message(FATAL "cannot find LLVM")
  endif()
  include(LLVMConfig)
  include(ClangConfig)
  include_directories(${LLVM_INCLUDE_DIR})
endmacro(configure_out_of_tree_llvm)

