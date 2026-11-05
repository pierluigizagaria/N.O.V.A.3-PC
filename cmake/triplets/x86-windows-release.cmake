# The release pipeline does not consume Debug dependencies. Keep the same x86
# dynamic CRT/DLL ABI as x86-windows without compiling every port twice.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
