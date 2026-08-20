# Внешние зависимости GUI: GLFW, Dear ImGui (docking), stb.
# Все тянутся через FetchContent; на CachyOS GLFW можно взять системный.
# Зафиксированы конкретные коммиты для воспроизводимости сборки.
include(FetchContent)

# --- GLFW: сперва системный (pacman -S glfw), иначе FetchContent -------------
find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND)
  message(STATUS "GLFW: системный")
else()
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  if(WIN32)
    # Windows: нативный Win32-бэкенд GLFW, ничего искать не нужно.
  else()
    # Wayland, если есть wayland-scanner; иначе X11.
    find_program(VW_WAYLAND_SCANNER wayland-scanner)
    find_path(VW_WAYLAND_H wayland-client.h)
    if(VW_WAYLAND_SCANNER AND VW_WAYLAND_H)
      set(GLFW_BUILD_WAYLAND ON  CACHE BOOL "" FORCE)
    else()
      message(STATUS "GLFW: wayland-scanner не найден — X11-only сборка")
      set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
    set(GLFW_BUILD_X11      ON  CACHE BOOL "" FORCE)
  endif()
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4)
  FetchContent_MakeAvailable(glfw)
endif()

# --- Dear ImGui (ветка docking) — пин на конкретный коммит ----------------------
# Обновляется вручную: см. https://github.com/ocornut/imgui/commits/docking
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        83f668625ad45364de71d385aeb6a5dd04bee02e) # 2026-08-06 docking
FetchContent_MakeAvailable(imgui)

add_library(imgui_glfw_gl3 STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_glfw_gl3 PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_glfw_gl3 PUBLIC glfw)
# Бэкенд imgui_impl_opengl3 использует собственный встроенный GL-загрузчик.

# --- stb (image / image_write) — пин на конкретный коммит ----------------------
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20) # 2026-08-02 master
FetchContent_MakeAvailable(stb)

add_library(stb_impl STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stb_impl.c)
target_include_directories(stb_impl PUBLIC ${stb_SOURCE_DIR})
