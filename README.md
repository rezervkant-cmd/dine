<div align="center">

# 🍽️ Dine

**Fast & modern Minecraft world viewer and exporter**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.24%2B-blue?style=flat-square&logo=cmake)](https://cmake.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%2B-blue?style=flat-square&logo=opengl)](https://www.opengl.org/)

Languages: 🇷🇺 [RU](#-ru) | 🇺🇸 [EN](#-en)

</div>

---

## 🇷🇺 RU

> ⚠️ **Внимание**
>
> Dine находится в активной разработке. Ожидайте багов и значительных изменений API/интерфейса до первого стабильного релиза.

### 💡 Что такое Dine?

**Dine** — это современный C++20 вьювер и экспортёр миров Minecraft, построенный с нуля с упором на производительность и удобство.

Приложение предоставляет гибкий GUI на базе **Dear ImGui** + **GLFW** + **OpenGL**, а также мощное ядро для работы с форматами Minecraft: NBT, Anvil-регионы, ресурспаки, моды и блок-модели.

### 🎯 Зачем использовать Dine?

Dine создан для тех, кто хочет:

- 🔥 **Быстро просматривать** миры Minecraft с топ-даун рендером и 3D-мешингом
- 📦 **Работать с модами** — поддержка `.jar`/`.zip` ресурспаков и мод-ассетов
- 🧊 **Экспортировать** сцены и блок-модели во внешние форматы
- ⚡ **Получать максимум производительности** — оптимизации под `x86-64-v3+`, LTO, `mold`/`lld`

### ✨ Возможности

| Компонент | Статус | Описание |
|-----------|--------|----------|
| 🗂️ NBT Parser | ✅ Готово | Чтение/запись Named Binary Tag |
| 🌍 Anvil Engine | ✅ Готово | Регионы, чанки, LZ4-компрессия |
| 🎨 Meshing | ✅ Готово | Генерация мешей блоков, атласы текстур |
| 🖼️ GUI Viewer | 🚧 WIP | Интерактивный просмотр мира (ImGui + OpenGL) |
| 📤 Exporter | 🚧 WIP | Экспорт сцен и моделей |
| 🧩 Mod Support | 🚧 WIP | Загрузка ассетов из модов и ресурспаков |
| 🗺️ Top-Down Map | ✅ Готово | 2D-рендер карты сверху |

### 🛠️ Сборка

```bash
# Клонирование
git clone https://github.com/rezervkant-cmd/dine.git
cd dine

# Создание build-директории
mkdir build && cd build

# Конфигурация (Release с нативными оптимизациями)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Сборка
cmake --build . --parallel

# Запуск тестов
ctest --output-on-failure
```

#### Зависимости

| Зависимость | Обязательная | Примечание |
|-------------|--------------|------------|
| CMake 3.24+ | ✅ Да | |
| C++20 компилятор | ✅ Да | GCC / Clang / MSVC |
| ZLIB | ✅ Да | Системная или встроенная (кросс-сборка) |
| GLFW3 | ⚡ Только GUI | Загружается через `cmake/Dependencies.cmake` |
| Dear ImGui | ⚡ Только GUI | Docking branch, загружается автоматически |
| Python 3 | ❌ Нет | Для генерации тестовых фикстур |
| mimalloc | ❌ Нет | `VW_USE_MIMALLOC=ON` для лучшей производительности |

#### Опции CMake

```cmake
option(VW_BUILD_GUI     "Собирать GUI-приложение" ON)
option(VW_BUILD_TESTS   "Собирать тесты ядра"     ON)
option(VW_USE_MIMALLOC  "Линковать mimalloc"      OFF)
option(VW_LTO           "Включить LTO"            ON)
set(VW_MARCH "native" CACHE STRING "Целевая микроархитектура")
set(VW_ENABLE_SANITIZERS "" CACHE STRING "Sanitizers: address | undefined")
```

### 🐧 Оптимизации для CachyOS / x86-64-v3+

Проект из коробки настроен на максимальную производительность:
- `-O3 -fno-plt -march=native` (GCC/Clang)
- LTO (Link-Time Optimization)
- Линковка через `mold`/`lld`, если доступен
- Поддержка `mimalloc` в качестве аллокатора

### 🤝 Участие в разработке

Pull requests и issue приветствуются! Перед началом работы:
1. Проверьте существующие issues
2. Соблюдайте стиль кода проекта (C++20, `snake_case`)
3. Убедитесь, что тесты проходят: `ctest`

---

## 🇺🇸 EN

> ⚠️ **Caution**
>
> Dine is currently in active development. Expect bugs and significant API/UI changes before the first stable release.

### 💡 What is Dine?

**Dine** is a modern C++20 Minecraft world viewer and exporter built from scratch with a focus on performance and usability.

The app provides a flexible GUI based on **Dear ImGui** + **GLFW** + **OpenGL**, as well as a powerful core for working with Minecraft formats: NBT, Anvil regions, resource packs, mods, and block models.

### 🎯 Why use Dine?

Dine is made for those who want to:

- 🔥 **Quickly browse** Minecraft worlds with top-down rendering and 3D meshing
- 📦 **Work with mods** — support for `.jar`/`.zip` resource packs and mod assets
- 🧊 **Export** scenes and block models to external formats
- ⚡ **Get maximum performance** — optimizations for `x86-64-v3+`, LTO, `mold`/`lld`

### ✨ Features

| Component | Status | Description |
|-----------|--------|-------------|
| 🗂️ NBT Parser | ✅ Ready | Read/write Named Binary Tag |
| 🌍 Anvil Engine | ✅ Ready | Regions, chunks, LZ4 compression |
| 🎨 Meshing | ✅ Ready | Block mesh generation, texture atlases |
| 🖼️ GUI Viewer | 🚧 WIP | Interactive world viewer (ImGui + OpenGL) |
| 📤 Exporter | 🚧 WIP | Scene and model export |
| 🧩 Mod Support | 🚧 WIP | Loading assets from mods and resource packs |
| 🗺️ Top-Down Map | ✅ Ready | 2D top-down map renderer |

### 🛠️ Building

```bash
# Clone
git clone https://github.com/rezervkant-cmd/dine.git
cd dine

# Create build directory
mkdir build && cd build

# Configure (Release with native optimizations)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --parallel

# Run tests
ctest --output-on-failure
```

#### Dependencies

| Dependency | Required | Note |
|------------|----------|------|
| CMake 3.24+ | ✅ Yes | |
| C++20 compiler | ✅ Yes | GCC / Clang / MSVC |
| ZLIB | ✅ Yes | System or bundled (cross-compilation) |
| GLFW3 | ⚡ GUI only | Fetched via `cmake/Dependencies.cmake` |
| Dear ImGui | ⚡ GUI only | Docking branch, auto-fetched |
| Python 3 | ❌ No | For generating test fixtures |
| mimalloc | ❌ No | `VW_USE_MIMALLOC=ON` for better performance |

#### CMake Options

```cmake
option(VW_BUILD_GUI     "Build GUI application" ON)
option(VW_BUILD_TESTS   "Build core tests"      ON)
option(VW_USE_MIMALLOC  "Link mimalloc"         OFF)
option(VW_LTO           "Enable LTO"            ON)
set(VW_MARCH "native" CACHE STRING "Target microarchitecture")
set(VW_ENABLE_SANITIZERS "" CACHE STRING "Sanitizers: address | undefined")
```

### 🐧 Optimizations for CachyOS / x86-64-v3+

The project is tuned for maximum performance out of the box:
- `-O3 -fno-plt -march=native` (GCC/Clang)
- LTO (Link-Time Optimization)
- Linking via `mold`/`lld` when available
- Optional `mimalloc` allocator support

### 🤝 Contributing

Pull requests and issues are welcome! Before contributing:
1. Check existing issues
2. Follow the project code style (C++20, `snake_case`)
3. Make sure tests pass: `ctest`

---

<div align="center">

Made with ❤️ and ☕ by <a href="https://github.com/rezervkant-cmd">rezervkant-cmd</a>

</div>
