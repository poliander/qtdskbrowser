# Cross-compiles qtdskbrowser into a 64-bit Windows binary using the
# mingw-w64 GCC cross-toolchain (`sudo apt install mingw-w64` on
# Debian/Ubuntu) together with a Qt6 build for win64_mingw.
#
# Usage:
#   cmake -B build-windows -S . \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
#       -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.y/mingw_64
#   cmake --build build-windows
#
# See the "Cross-compiling for Windows" section in README.md for how to
# obtain a matching Qt6/mingw-w64 pair.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Qt lives outside CMAKE_FIND_ROOT_PATH (it's pointed to via
# CMAKE_PREFIX_PATH, not installed under the mingw-w64 sysroot), so package
# lookups need to fall back to searching it directly as well.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Qt6's own build tools (moc, uic, rcc) must run on the host, but a
# win64_mingw Qt kit only ships Windows .exe versions of them. Point Qt at a
# native Qt6 installation (e.g. the one from qt6-base-dev) to run those from
# instead; override with -DQT_HOST_PATH=... if that is somewhere else.
if (NOT DEFINED QT_HOST_PATH)
    set(QT_HOST_PATH /usr CACHE PATH "Path to a native Qt6 install, used to run moc/uic/rcc")
endif()
