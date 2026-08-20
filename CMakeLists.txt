cmake_minimum_required(VERSION 3.24)
project(dine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------
# Опции
# ----------------------------------------------------------------------------
option(VW_BUILD_GUI     "Собирать GUI-приложение (GLFW + ImGui + OpenGL)" ON)
option(VW_BUILD_TESTS   "Собирать тесты ядра"                             ON)
option(VW_USE_MIMALLOC  "Линковать mimalloc (pacman -S mimalloc)"         OFF)
option(VW_LTO           "Включить межмодульную оптимизацию (LTO)"         ON)
set(VW_ENABLE_SANITIZERS "" CACHE STRING
    "Sanitizers for Debug/CI: empty | address | undefined | address,undefined")
set(VW_MARCH "native" CACHE STRING "Целевая микроархитектура: native | x86-64-v3 | x86-64-v4 | off")

if(VW_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "VW_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
  string(REPLACE "," ";" VW_SANITIZER_LIST "${VW_ENABLE_SANITIZERS}")
  foreach(VW_SANITIZER IN LISTS VW_SANITIZER_LIST)
    if(NOT VW_SANITIZER MATCHES "^(address|undefined)$")
      message(FATAL_ERROR "Unsupported sanitizer: ${VW_SANITIZER}")
    endif()
  endforeach()
  string(REPLACE ";" "," VW_SANITIZER_FLAGS "${VW_SANITIZER_LIST}")
  message(STATUS "Dine sanitizers: ${VW_SANITIZER_FLAGS}")
  add_compile_options(-fsanitize=${VW_SANITIZER_FLAGS} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${VW_SANITIZER_FLAGS})
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Флаги оптимизации под CachyOS / x86-64-v3+
# ----------------------------------------------------------------------------
add_library(vw_flags INTERFACE)
if(MSVC)
  # MSVC не понимает флаги GCC/Clang (-O3, -fno-plt, -march, -pipe) —
  # для Visual Studio используем их эквиваленты.
  target_compile_options(vw_flags INTERFACE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
    /W4 /EHsc /permissive- /MP /utf-8
  )
  if(NOT VW_MARCH STREQUAL "off")
    # У MSVC нет -march=native; проект ориентирован на x86-64-v3+.
    if(VW_MARCH STREQUAL "x86-64-v4")
      target_compile_options(vw_flags INTERFACE /arch:AVX512)
    else()
      target_compile_options(vw_flags INTERFACE /arch:AVX2)
    endif()
  endif()
else()
  target_compile_options(vw_flags INTERFACE
    $<$<CONFIG:Release>:-O3 -fno-plt -fomit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    -Wall -Wextra -Wshadow
    -pipe
  )
  if(NOT VW_MARCH STREQUAL "off")
    if(VW_MARCH STREQUAL "native")
      target_compile_options(vw_flags INTERFACE -march=native -mtune=native)
    else()
      # уровни x86-64-v2/v3/v4 не являются валидными значениями -mtune
      target_compile_options(vw_flags INTERFACE -march=${VW_MARCH} -mtune=generic)
    endif()
  endif()
endif()

if(VW_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT VW_IPO_OK OUTPUT VW_IPO_MSG)
  if(VW_IPO_OK)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(WARNING "LTO недоступен: ${VW_IPO_MSG}")
  endif()
endif()

# Linker: у CachyOS по умолчанию mold/lld — используем, если есть
# (только GCC/Clang: MSVC-линкер флаг -fuse-ld не понимает)
if(NOT MSVC)
  find_program(VW_MOLD mold)
  if(VW_MOLD)
    add_link_options(-fuse-ld=mold)
  endif()
endif()

# ----------------------------------------------------------------------------
# Зависимости
# ----------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING AND WIN32)
  # Кросс-сборка под Windows: zlib статически из исходников.
  include(FetchContent)
  FetchContent_Declare(zlib_src
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    SOURCE_SUBDIR non-existent-skip-add-subdirectory)
  FetchContent_MakeAvailable(zlib_src)
  file(GLOB VW_ZLIB_SOURCES ${zlib_src_SOURCE_DIR}/*.c)
  add_library(vw_zlib_impl STATIC ${VW_ZLIB_SOURCES})
  target_include_directories(vw_zlib_impl PUBLIC ${zlib_src_SOURCE_DIR})
  add_library(vw_zlib ALIAS vw_zlib_impl)
else()
  find_package(ZLIB REQUIRED)
  add_library(vw_zlib ALIAS ZLIB::ZLIB)
endif()
find_package(Threads REQUIRED)

if(VW_USE_MIMALLOC)
  find_package(mimalloc REQUIRED)
endif()

if(VW_BUILD_GUI)
  include(cmake/Dependencies.cmake)   # GLFW, ImGui (docking), stb
endif()

# ----------------------------------------------------------------------------
# Ядро (без GUI): NBT, Anvil, моды, meshing, экспорт
# ----------------------------------------------------------------------------
add_library(vw_core STATIC
  src/nbt/nbt.cpp
  src/anvil/region.cpp
  src/anvil/lz4_block.cpp
  src/anvil/world.cpp
  src/mods/zipfile.cpp
  src/mods/mod_assets.cpp
  src/mods/bbmodel.cpp
  src/mesh/grid.cpp
  src/mesh/atlas.cpp
  src/render/mc_mesher.cpp
  src/render/top_map.cpp
  src/mods/block_model.cpp
  src/export/mc_export.cpp
)
target_include_directories(vw_core PUBLIC src)
target_link_libraries(vw_core PUBLIC vw_flags vw_zlib Threads::Threads)
if(VW_USE_MIMALLOC)
  target_link_libraries(vw_core PUBLIC mimalloc)
endif()

# ----------------------------------------------------------------------------
# GUI-приложение
# ----------------------------------------------------------------------------
if(VW_BUILD_GUI)
  # WIN32 — GUI-подсистема на Windows, без консольного окна.
  add_executable(dine WIN32
    src/app/main.cpp
    src/app/gl_loader.cpp
    src/app/vanilla_fetch.cpp
    src/app/renderer.cpp
  )
  if(WIN32)
    # Иконка exe (packaging/icon/app.ico) + версия файла — через ресурсы Windows.
    enable_language(RC)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/app.ico" DINE_ICON_ABS)
    set(DINE_ICON_PATH "${DINE_ICON_ABS}")   # forward-slash путь (windres понимает)
    configure_file(src/app/icon.rc.in icon.rc @ONLY)
    target_sources(dine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
  endif()
  target_link_libraries(dine PRIVATE vw_core glfw imgui_glfw_gl3 stb_impl)
  # Не включать системный GL/gl.h из GLFW — используем собственный загрузчик (gl_loader.hpp)
  target_compile_definitions(dine PRIVATE GLFW_INCLUDE_NONE)
endif()

# ----------------------------------------------------------------------------
# Тесты
# ----------------------------------------------------------------------------
if(VW_BUILD_TESTS)
  enable_testing()
  add_executable(vw_test_core tests/test_core.cpp)
  target_link_libraries(vw_test_core PRIVATE vw_core)
  add_test(NAME core COMMAND vw_test_core ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
  # Визуально-численный тест моделей/мешера (костёр, забор, uvlock, повороты)
  add_executable(vw_test_models tests/test_models.cpp)
  add_executable(vw_debug_models tests/debug_models.cpp)
  target_link_libraries(vw_debug_models PRIVATE vw_core)
  target_link_libraries(vw_test_models PRIVATE vw_core)
  add_test(NAME models COMMAND vw_test_models ${CMAKE_CURRENT_SOURCE_DIR}/tests/data
               ${CMAKE_CURRENT_SOURCE_DIR}/tests/out)

  # Автогенерация тестовых фикстур, если доступен Python.
  # Генераторы полностью офлайн и детерминированы, поэтому запускаем их ровно
  # один раз — по stamp-файлу, а не целью ALL при каждой сборке.
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(VW_FIXTURE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/test_fixtures.stamp)
    add_custom_command(
      OUTPUT ${VW_FIXTURE_STAMP}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_world.py
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_deco_test_jar.py
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/mods/decomini.jar
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_vanilla_test_jar.py
      COMMAND ${CMAKE_COMMAND} -E touch ${VW_FIXTURE_STAMP}
      DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_world.py
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_deco_test_jar.py
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_vanilla_test_jar.py
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vanilla_assets.json
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Генерация тестовых фикстур (worlds, mods, vanilla)"
      VERBATIM
    )
    add_custom_target(vw_test_data DEPENDS ${VW_FIXTURE_STAMP})
    # Гарантируем, что фикстуры сгенерированы до тестов
    add_dependencies(vw_test_core vw_test_data)
    add_dependencies(vw_test_models vw_test_data)
    add_dependencies(vw_debug_models vw_test_data)
  else()
    message(WARNING "Python3 не найден — тестовые фикстуры не будут генерироваться автоматически. Запустите tests/make_test_world.py вручную.")
  endif()
endif()
