add_library(oc_vfs STATIC
  vfs.h
  vfs.cpp
  mods.h
  mods.cpp
)
target_link_libraries(oc_vfs PUBLIC oc_core PRIVATE miniz::miniz)
