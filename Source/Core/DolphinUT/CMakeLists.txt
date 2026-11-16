# Ubuntu Touch frontend: a touch-first QML UI modeled on the Android app (docs/ut-port/stage3-roadmap.md).

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Quick DBus)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10.0)
  find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

add_executable(dolphin-ut
  main.cpp
  Host.cpp
  Emulator.cpp
  Emulator.h
  GameList.cpp
  GameList.h
  GameWindow.cpp
  GameWindow.h
  LomiriIntegration.cpp
  LomiriIntegration.h
  TouchOverlay.cpp
  TouchOverlay.h
)

# The touch overlay drives the same InputOverrider the Android overlay uses. InputCommon only builds
# it for Android, so build it here instead of changing InputCommon.
target_sources(dolphin-ut PRIVATE
  ${PROJECT_SOURCE_DIR}/Source/Core/InputCommon/ControllerInterface/Touch/InputOverrider.cpp
)

set_target_properties(dolphin-ut PROPERTIES AUTOMOC ON)

# QTP0001: put the module under qrc:/qt/qml/, which is on the QML import path, so
# QQmlApplicationEngine::loadFromModule("DolphinUT", "Main") finds it.
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
qt_add_qml_module(dolphin-ut
  URI DolphinUT
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/LibraryPage.qml
    qml/GameCard.qml
    qml/GameDetailsPage.qml
    qml/GameFoldersPage.qml
    qml/FolderPickerPage.qml
    qml/DeveloperPage.qml
)

target_include_directories(dolphin-ut PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})

target_link_libraries(dolphin-ut
PRIVATE
  core
  uicommon
  cpp-optparse
  inputcommon
  videocommon
  imgui
  Qt6::Quick
  Qt6::DBus
)
if(TARGET Qt6::GuiPrivate)
  target_link_libraries(dolphin-ut PRIVATE Qt6::GuiPrivate)
endif()
