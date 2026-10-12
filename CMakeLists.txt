cmake_minimum_required(VERSION 3.20)
project(myRTOS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warnings across all targets
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif (MSVC)
    add_compile_options(/W4)
endif()

# -------------------------------------------------------------
# 1. RTOS Kernel Core Library (Freestanding, No Exceptions/RTTI)
# -------------------------------------------------------------
add_library(myrtos_kernel STATIC
    kernel/src/scheduler.cpp
    kernel/src/semaphore.cpp
    kernel/src/mutex.cpp
    kernel/src/queue.cpp
    kernel/src/memory_pool.cpp
    kernel/src/timer.cpp
)

target_include_directories(myrtos_kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(myrtos_kernel PRIVATE -fno-exceptions -fno-rtti)
elseif (MSVC)
    target_compile_options(myrtos_kernel PRIVATE /EHs-c- /GR-)
endif()

# -------------------------------------------------------------
# 2. Host Port Layer (Win32 Fibers on Windows, POSIX ucontext on Linux)
# -------------------------------------------------------------
if (WIN32)
    set(PORT_SOURCES port/simulator/port_win32.cpp)
else()
    set(PORT_SOURCES port/simulator/port_posix.cpp)
endif()

add_library(myrtos_port STATIC
    ${PORT_SOURCES}
)

target_include_directories(myrtos_port PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
)

target_link_libraries(myrtos_port PUBLIC myrtos_kernel)
if (WIN32)
    target_link_libraries(myrtos_port PRIVATE kernel32 winmm)
else()
    target_link_libraries(myrtos_port PRIVATE pthread)
endif()

# -------------------------------------------------------------
# 3. Virtual Hardware & Peripheral Subsystem
# -------------------------------------------------------------
add_library(myrtos_hw STATIC
    hw/src/vic.cpp
    hw/src/gpio.cpp
    hw/src/uart.cpp
)

target_include_directories(myrtos_hw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
)

target_link_libraries(myrtos_hw PUBLIC myrtos_kernel myrtos_port)

# -------------------------------------------------------------
# 4. Third-Party UI: GLFW & Dear ImGui
# -------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt")
    add_subdirectory(third_party/glfw)
else()
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
else()
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_DIR "${imgui_SOURCE_DIR}")
endif()

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

if (WIN32)
    target_link_libraries(imgui PUBLIC glfw opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
endif()

# -------------------------------------------------------------
# 5. UI Observability & Dashboard Library
# -------------------------------------------------------------
add_library(myrtos_ui STATIC
    ui/src/dashboard.cpp
)

target_include_directories(myrtos_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
)

if (WIN32)
    target_link_libraries(myrtos_ui PUBLIC
        myrtos_kernel
        myrtos_hw
        imgui
        glfw
        opengl32
    )
else()
    target_link_libraries(myrtos_ui PUBLIC
        myrtos_kernel
        myrtos_hw
        imgui
        glfw
        OpenGL::GL
        pthread
    )
endif()

# -------------------------------------------------------------
# 6. Verification Test Suite Executable
# -------------------------------------------------------------
add_executable(myrtos_tests
    tests/main_tests.cpp
)

target_include_directories(myrtos_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
)

target_link_libraries(myrtos_tests PRIVATE
    myrtos_kernel
    myrtos_port
    myrtos_hw
)

# -------------------------------------------------------------
# 7. Interactive Simulator CLI Demo
# -------------------------------------------------------------
add_executable(myrtos_sim
    examples/main_sim.cpp
)

target_include_directories(myrtos_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
)

target_link_libraries(myrtos_sim PRIVATE
    myrtos_kernel
    myrtos_port
    myrtos_hw
)

# -------------------------------------------------------------
# 8. Interactive Graphical Simulation Dashboard Executable
# -------------------------------------------------------------
add_executable(myrtos_dashboard
    examples/main_gui.cpp
)

target_include_directories(myrtos_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
)

target_link_libraries(myrtos_dashboard PRIVATE
    myrtos_ui
    myrtos_kernel
    myrtos_port
    myrtos_hw
)

# Standalone static linkage for MinGW
if (MINGW)
    target_link_options(myrtos_tests PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(myrtos_sim PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(myrtos_dashboard PRIVATE -static -static-libgcc -static-libstdc++)
endif()
