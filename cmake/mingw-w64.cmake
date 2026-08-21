# cmake/mingw-w64.cmake — тулчейн для кросс-сборки Windows x64 из Linux.
# Использование: cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Суффикс -posix (Debian/Ubuntu); на Arch — без суффикса
find_program(VW_MINGW_GXX NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++)
find_program(VW_MINGW_GCC NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc)
find_program(VW_MINGW_RC  NAMES x86_64-w64-mingw32-windres)
set(CMAKE_C_COMPILER   ${VW_MINGW_GCC})
set(CMAKE_CXX_COMPILER ${VW_MINGW_GXX})
if(VW_MINGW_RC)
  set(CMAKE_RC_COMPILER ${VW_MINGW_RC})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Статическая линковка рантаймов — один .exe без DLL-зависимостей MinGW.
add_link_options(-static -static-libgcc -static-libstdc++)
