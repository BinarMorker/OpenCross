# Engine asset types (header-only for now).
add_library(oc_assets INTERFACE)
target_include_directories(oc_assets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(oc_assets INTERFACE oc_core)
