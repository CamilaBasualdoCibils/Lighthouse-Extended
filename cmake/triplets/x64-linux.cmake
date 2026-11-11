set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Use the unmodified Graphviz port with shared layout plugins.
if(PORT STREQUAL "graphviz")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
