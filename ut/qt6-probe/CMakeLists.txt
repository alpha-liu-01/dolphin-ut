cmake_minimum_required(VERSION 3.25)
project(qt6-probe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10.0)
  find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

add_executable(qt6-probe main.cpp)
target_link_libraries(qt6-probe PRIVATE Qt6::Widgets Qt6::OpenGL)
if(TARGET Qt6::GuiPrivate)
  target_link_libraries(qt6-probe PRIVATE Qt6::GuiPrivate)
else()
  target_include_directories(qt6-probe PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
endif()
