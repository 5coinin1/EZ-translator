cmake_minimum_required(VERSION 3.24)
project(EZTranslator LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt6 ---
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()
set(CMAKE_AUTOUIC OFF)

# --- Nguồn ---
set(SOURCES
    src/main.cpp

    # Core
    src/core/Types.h
    src/core/WindowEnumerator.h
    src/core/WindowEnumerator.cpp

    # Capture – GDI window capture pipeline
    src/capture/IWindowCapture.h
    src/capture/GdiWindowCapture.h
    src/capture/GdiWindowCapture.cpp

    # Region – ROI extract pipeline
    src/region/RegionFrame.h
    src/region/RegionManager.h
    src/region/RegionManager.cpp

    # App
    src/app/AppController.h
    src/app/AppController.cpp

    # UI – theme
    src/ui/theme/StyleTheme.h
    src/ui/theme/StyleTheme.cpp
    src/ui/theme/IconFactory.h
    src/ui/theme/IconFactory.cpp

    # UI – reusable components
    src/ui/components/TitleBar.h
    src/ui/components/TitleBar.cpp
    src/ui/components/SectionTitle.h
    src/ui/components/SectionTitle.cpp
    src/ui/components/AppComboBox.h
    src/ui/components/AppComboBox.cpp
    src/ui/components/PrimaryButton.h
    src/ui/components/PrimaryButton.cpp
    src/ui/components/SecondaryButton.h
    src/ui/components/SecondaryButton.cpp
    src/ui/components/IconButton.h
    src/ui/components/IconButton.cpp
    src/ui/components/StatusIndicator.h
    src/ui/components/StatusIndicator.cpp
    src/ui/components/DisplayModeCard.h
    src/ui/components/DisplayModeCard.cpp
    src/ui/components/ToggleSwitch.h
    src/ui/components/ToggleSwitch.cpp

    # UI – Region editor graphics & overlay
    src/ui/region/RoiGraphicsItem.h
    src/ui/region/RoiGraphicsItem.cpp
    src/ui/region/RoiCommands.h
    src/ui/region/RoiCommands.cpp
    src/ui/region/RegionSnipperOverlay.h
    src/ui/region/RegionSnipperOverlay.cpp
    src/ui/region/RegionHighlightOverlay.h
    src/ui/region/RegionHighlightOverlay.cpp

    # UI – Windows
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/RegionEditorWindow.h
    src/ui/RegionEditorWindow.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/RoiPreviewDialog.h
    src/ui/RoiPreviewDialog.cpp
    src/ui/MiniFloatBar.h
    src/ui/MiniFloatBar.cpp
    src/ui/TrayIconManager.h
    src/ui/TrayIconManager.cpp

    # Resources
    resources/resources.qrc
)

qt_add_executable(EZTranslator WIN32 ${SOURCES})

target_include_directories(EZTranslator PRIVATE src)

target_link_libraries(EZTranslator
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Win32 libs (cho platform layer sau này)
if(WIN32)
    target_link_libraries(EZTranslator PRIVATE
        user32
        gdi32
        dwmapi
        shell32
    )
endif()

# UTF-8 source encoding (MinGW/GCC)
if(MINGW OR GCC)
    target_compile_options(EZTranslator PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# --- Unit Tests ---
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    add_executable(test_region_manager
        tests/test_region_manager.cpp
        src/region/RegionManager.h
        src/region/RegionManager.cpp
        src/region/RegionFrame.h
        src/core/Types.h
        src/capture/IWindowCapture.h
    )
    target_include_directories(test_region_manager PRIVATE src)
    target_link_libraries(test_region_manager PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    if(MINGW OR GCC)
        target_compile_options(test_region_manager PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
endif()
