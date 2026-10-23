# Title screen, name entry and the Continue slot list.
add_library(oc_title STATIC
  nav.h
  canvas.h
  canvas.cpp
  name_codec.h
  name_codec.cpp
  name_entry.h
  name_entry.cpp
  title_screen.h
  title_screen.cpp
  boot_logo.h
  slot_list.h
  slot_list.cpp
)
target_link_libraries(oc_title PUBLIC oc_ui oc_audio oc_save)
