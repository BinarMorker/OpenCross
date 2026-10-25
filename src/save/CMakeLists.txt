add_library(oc_save STATIC
  crypto.h
  crypto.cpp
  encrypted_zip.h
  encrypted_zip.cpp
  memcard.h
  memcard.cpp
  cc_save.h
  cc_save.cpp
  ext_save.h
  ext_save.cpp
  oc_saves.h
  oc_saves.cpp
)
target_link_libraries(oc_save PUBLIC oc_core PRIVATE miniz::miniz)
