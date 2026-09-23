cmake_minimum_required(VERSION 3.24)
project(EZTranslator LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt6 ---
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "$ENV{QTDIR}" CACHE PATH "Qt install prefix")
endif()
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

    # Capture – Windows.Graphics.Capture (qua helper process + shared memory)
    src/capture/WgcSharedMemory.h
    src/capture/WgcWindowCapture.h
    src/capture/WgcWindowCapture.cpp

    # Region – ROI extract pipeline
    src/region/RegionFrame.h
    src/region/RegionManager.h
    src/region/RegionManager.cpp

    # Detection – change detection + text region detection
    src/detection/TextBox.h
    src/detection/ITextDetector.h
    src/detection/HeuristicTextDetector.h
    src/detection/HeuristicTextDetector.cpp
    src/detection/RecursiveXYCut.h
    src/detection/RecursiveXYCut.cpp
    src/detection/ChangeDetector.h
    src/detection/ChangeDetector.cpp

    # Overlay – hiển thị chữ OCR/dịch đè lên cửa sổ đích
    src/overlay/TextOverlay.h
    src/overlay/TextOverlay.cpp

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

# --- ONNX Runtime (DirectML build, vendored ở third_party/) ---
set(ORT_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if(NOT EXISTS "${ORT_DIR}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime không tìm thấy ở ${ORT_DIR}. "
                        "Cần include/, lib/onnxruntime.lib và bin/*.dll.")
endif()

# --- Module OCR (static lib) — Detection (PP-OCR DB) + Recognition (PP-OCR CTC) ---
add_library(ez_ocr STATIC
    src/ocr/OcrTypes.h
    src/ocr/OcrRuntime.h
    src/ocr/OcrRuntime.cpp
    src/ocr/OcrDetector.h
    src/ocr/OcrDetector.cpp
    src/ocr/OcrRecognizer.h
    src/ocr/OcrRecognizer.cpp
    src/ocr/OcrTextAssembler.h
    src/ocr/OcrTextAssembler.cpp
    src/ocr/OcrEngine.h
    src/ocr/OcrEngine.cpp
)
target_include_directories(ez_ocr PUBLIC src PRIVATE "${ORT_DIR}/include")
target_link_libraries(ez_ocr PUBLIC Qt6::Gui PRIVATE "${ORT_DIR}/lib/onnxruntime.lib")

# Model mặc định: cho phép override bằng biến môi trường EZ_MODELS_DIR lúc chạy.
target_compile_definitions(ez_ocr PUBLIC
    EZ_DEFAULT_MODELS_DIR="${CMAKE_SOURCE_DIR}/models")

target_include_directories(EZTranslator PRIVATE src)

target_link_libraries(EZTranslator
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ez_ocr
)

# DLL của ONNX Runtime + DirectML phải nằm cạnh exe lúc chạy.
file(GLOB ORT_RUNTIME_DLLS "${ORT_DIR}/bin/*.dll")
add_custom_command(TARGET EZTranslator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
        "$<TARGET_FILE_DIR:EZTranslator>")

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

# --- Windows.Graphics.Capture helper (Qt-free, chạy ở process riêng) ---
if(WIN32)
    # GCC devirtualization (bật từ -O2) miscompile các virtual call của COM
    # interface khai báo tay của WGC → crash. -fno-devirtualize khắc phục.
    set_source_files_properties(src/capture/wgc/WgcCapture.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-devirtualize")

    add_executable(wgc_capture_helper
        tools/wgc_capture_helper.cpp
        src/capture/WgcSharedMemory.h
        src/capture/wgc/WgcCapture.h
        src/capture/wgc/WgcCapture.cpp
    )
    target_include_directories(wgc_capture_helper PRIVATE src)
    target_link_libraries(wgc_capture_helper PRIVATE user32 gdi32 d3d11 dxgi runtimeobject)
    set_target_properties(wgc_capture_helper PROPERTIES
        WIN32_EXECUTABLE OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

    add_dependencies(EZTranslator wgc_capture_helper)
endif()

# --- OCR smoke test (console) — chạy det+rec trên ảnh tổng hợp ---
add_executable(ocr_smoke tools/ocr_smoke.cpp)
target_link_libraries(ocr_smoke PRIVATE ez_ocr Qt6::Gui)
set_target_properties(ocr_smoke PROPERTIES WIN32_EXECUTABLE OFF)
if(WIN32)
    add_custom_command(TARGET ocr_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:ocr_smoke>")
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
    set_target_properties(test_region_manager PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MINGW OR GCC)
        target_compile_options(test_region_manager PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()

    add_executable(test_detection
        tests/test_detection.cpp
        src/detection/TextBox.h
        src/detection/ITextDetector.h
        src/detection/HeuristicTextDetector.h
        src/detection/HeuristicTextDetector.cpp
        src/detection/RecursiveXYCut.h
        src/detection/RecursiveXYCut.cpp
        src/detection/ChangeDetector.h
        src/detection/ChangeDetector.cpp
    )
    target_include_directories(test_detection PRIVATE src)
    target_link_libraries(test_detection PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    set_target_properties(test_detection PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MINGW OR GCC)
        target_compile_options(test_detection PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
endif()
