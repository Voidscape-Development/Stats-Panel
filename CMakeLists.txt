cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_BUILD_LEGACY "Enable legacy build" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
)

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/vitals-common.cpp
    src/vitals-common.hpp
    src/vitals-controller.cpp
    src/vitals-controller.hpp
    src/vitals-display.cpp
    src/vitals-display.hpp
    src/vitals-gpu.hpp
    src/vitals-sampler.cpp
    src/vitals-sampler.hpp
    src/vitals-settings-dialog.cpp
    src/vitals-settings-dialog.hpp
    src/vitals-settings.cpp
    src/vitals-settings.hpp
    src/vitals-view.cpp
    src/vitals-view.hpp
    src/vitals-worker.cpp
    src/vitals-worker.hpp
)

if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/vitals-gpu-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE pdh)
else()
  # Vitals ships for Windows only; the stub keeps other platforms buildable for development.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/vitals-gpu-stub.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
