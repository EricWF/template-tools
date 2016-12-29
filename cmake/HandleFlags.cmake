# Turn a comma separated CMake list into a space separated string.
macro(split_list listname)
  string(REPLACE ";" " " ${listname} "${${listname}}")
endmacro()

macro(make_list listname)
  string(REPLACE " " ";" ${listname} "${${listname}}")
endmacro()
