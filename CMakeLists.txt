cmake_minimum_required(VERSION 3.24)
project(EZTranslator LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Qt6 ---
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "$ENV{QTDIR}" CACHE PATH "Qt install prefix")
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
qt_standard_project_setup()
set(CMAKE_AUTOUIC OFF)

# --- Nguồn ---
set(SOURCES
    src/main.cpp

    # Core
    src/core/Types.h
    src/core/WindowEnumerator.h
    src/core/WindowEnumerator.cpp
    src/core/SettingsStore.h
    src/core/SettingsStore.cpp

    # Platform – tiện ích Windows (loại khỏi capture, autostart, phím tắt toàn cục)
    src/platform/PlatformUtils.h
    src/platform/PlatformUtils.cpp
    src/platform/GlobalHotkeys.h
    src/platform/GlobalHotkeys.cpp
    src/platform/FramelessWindowHelper.h
    src/platform/FramelessWindowHelper.cpp

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
    src/detection/ImageHash.h
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
    src/overlay/OverlayLayout.h
    src/overlay/OverlayLayout.cpp
    src/scroll/ScrollEstimator.h
    src/scroll/ScrollEstimator.cpp
    src/scroll/ScrollTracker.h
    src/scroll/ScrollTracker.cpp
    src/scroll/ScrollOcrReuse.h
    src/scroll/ScrollOcrReuse.cpp
    src/overlay/ParagraphAnchor.h
    src/overlay/ParagraphAnchor.cpp
    src/overlay/TextFlow.h
    src/overlay/TextFlow.cpp
    src/overlay/TranslationPopup.h
    src/overlay/TranslationPopup.cpp

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
    src/ui/FloatingTranslationWindow.h
    src/ui/FloatingTranslationWindow.cpp
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
    src/ocr/OcrScheduler.h
    src/ocr/OcrScheduler.cpp
    src/ocr/OcrRegionMailbox.h
    src/ocr/OcrRegionMailbox.cpp
    src/ocr/OcrThreadBudget.h
    src/ocr/OcrThreadBudget.cpp
    src/ocr/OcrWorker.h
    src/ocr/OcrWorker.cpp
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
    # Smart-cover plate: dựng trên luồng OCR (không phải luồng UI) — xem OcrWorker::recognizeRegion.
    src/overlay/TextCover.h
    src/overlay/TextCover.cpp
)
target_include_directories(ez_ocr PUBLIC src PRIVATE "${ORT_DIR}/include")
target_link_libraries(ez_ocr PUBLIC Qt6::Gui PRIVATE "${ORT_DIR}/lib/onnxruntime.lib")

# --- Module Translation (static lib) — cache + engine online + ghép đoạn ---
# sentencepiece cho engine dịch offline (Marian/OPUS-MT).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # sentencepiece dùng cmake_minimum_required cũ
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/sentencepiece
                 ${CMAKE_BINARY_DIR}/sentencepiece EXCLUDE_FROM_ALL)

add_library(ez_translate STATIC
    src/translation/ITranslator.h
    src/translation/TranslationCache.h
    src/translation/TranslationCache.cpp
    src/translation/TranslationPost.h
    src/translation/TranslationPost.cpp
    src/translation/LanguageDetect.h
    src/translation/LanguageDetect.cpp
    src/translation/DocumentTranslator.h
    src/translation/DocumentTranslator.cpp
    src/translation/TranslationConfig.h
    src/translation/TranslationThreadBudget.h
    src/translation/TranslationThreadBudget.cpp
    src/translation/TranslationCircuitBreaker.h
    src/translation/TranslationCircuitBreaker.cpp
    src/translation/TranslationProbe.h
    src/translation/TranslationProbe.cpp
    src/translation/GoogleTranslator.h
    src/translation/GoogleTranslator.cpp
    src/translation/AiTranslator.h
    src/translation/AiTranslator.cpp
    src/translation/MyMemoryTranslator.h
    src/translation/MyMemoryTranslator.cpp
    src/translation/MarianTranslator.h
    src/translation/MarianTranslator.cpp
    src/translation/CTranslate2Translator.h
    src/translation/CTranslate2Translator.cpp
    src/translation/TranslationWorker.h
    src/translation/TranslationWorker.cpp
)
target_include_directories(ez_translate PUBLIC src
    PRIVATE "${ORT_DIR}/include" "${CMAKE_SOURCE_DIR}/third_party/sentencepiece/src")
target_compile_definitions(ez_translate PUBLIC
    EZ_DEFAULT_MODELS_DIR="${CMAKE_SOURCE_DIR}/models"
    EZ_CT2_SCRIPT="${CMAKE_SOURCE_DIR}/tools/ct2_server.py")
target_link_libraries(ez_translate PUBLIC Qt6::Core Qt6::Network
    PRIVATE "${ORT_DIR}/lib/onnxruntime.lib" sentencepiece)

# Model mặc định: cho phép override bằng biến môi trường EZ_MODELS_DIR lúc chạy.
target_compile_definitions(ez_ocr PUBLIC
    EZ_DEFAULT_MODELS_DIR="${CMAKE_SOURCE_DIR}/models")

target_include_directories(EZTranslator PRIVATE src)

target_link_libraries(EZTranslator
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    ez_ocr
    ez_translate
)

# DLL của ONNX Runtime + DirectML phải nằm cạnh exe lúc chạy.
file(GLOB ORT_RUNTIME_DLLS "${ORT_DIR}/bin/*.dll")
add_custom_command(TARGET EZTranslator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
        "$<TARGET_FILE_DIR:EZTranslator>")

# Qt6Concurrent.dll (nút "Kiểm tra engine" chạy QtConcurrent::run) — windeployqt cũng copy,
# nhưng build nội bộ không chạy windeployqt nên copy tường minh để app chạy được ngay.
if(WIN32)
    set(QT_CONCURRENT_RUNTIME_DLL "")
    foreach(_prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION)
        get_target_property(_loc Qt6::Concurrent ${_prop})
        if(_loc AND EXISTS "${_loc}")
            set(QT_CONCURRENT_RUNTIME_DLL "${_loc}")
            break()
        endif()
    endforeach()
    if(QT_CONCURRENT_RUNTIME_DLL)
        add_custom_command(TARGET EZTranslator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_CONCURRENT_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:EZTranslator>")
    endif()
endif()

# Sidecar dịch ct2 phải nằm cạnh exe. Copy ở MỖI lần build (không chỉ lúc configure):
# sửa tools/ct2_server.py rồi build lại trước đây để lại bản cũ trong build/.
add_custom_command(TARGET EZTranslator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/tools/ct2_server.py"
        "$<TARGET_FILE_DIR:EZTranslator>/ct2_server.py")

# Win32 libs (cho platform layer sau này)
if(WIN32)
    target_link_libraries(EZTranslator PRIVATE
        user32
        gdi32
        dxgi
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
target_link_libraries(ocr_smoke PRIVATE ez_ocr ez_translate Qt6::Gui)
set_target_properties(ocr_smoke PROPERTIES WIN32_EXECUTABLE OFF)
if(WIN32)
    add_custom_command(TARGET ocr_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:ocr_smoke>")
endif()

# --- Translation smoke test (console) — dịch vài câu qua engine online ---
add_executable(translate_smoke tools/translate_smoke.cpp)
target_link_libraries(translate_smoke PRIVATE ez_translate)
set_target_properties(translate_smoke PROPERTIES WIN32_EXECUTABLE OFF)

# --- Settings smoke (console) — kiểm chứng SettingsStore lưu/nạp/migrate đủ mọi trường ---
add_executable(settings_smoke
    tools/settings_smoke.cpp
    src/core/SettingsStore.h
    src/core/SettingsStore.cpp
    src/core/Types.h
)
target_include_directories(settings_smoke PRIVATE src)
target_link_libraries(settings_smoke PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(settings_smoke PROPERTIES WIN32_EXECUTABLE OFF)

# --- Probe smoke (console) — chạy TranslationProbe (nút "Kiểm tra engine") từ CLI ---
add_executable(probe_smoke tools/probe_smoke.cpp)
target_link_libraries(probe_smoke PRIVATE ez_translate)
set_target_properties(probe_smoke PROPERTIES WIN32_EXECUTABLE OFF)

# --- Settings preview (console, offscreen) — render các trang Cài đặt ra PNG để soi UI ---
add_executable(settings_preview
    tools/settings_preview.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/theme/StyleTheme.h
    src/ui/theme/StyleTheme.cpp
    src/ui/components/PrimaryButton.h
    src/ui/components/PrimaryButton.cpp
    src/ui/components/SecondaryButton.h
    src/ui/components/SecondaryButton.cpp
    src/ui/components/ToggleSwitch.h
    src/ui/components/ToggleSwitch.cpp
    src/platform/FramelessWindowHelper.h
    src/platform/FramelessWindowHelper.cpp
    src/core/Types.h
    src/core/SettingsStore.h
    src/core/SettingsStore.cpp
)
target_include_directories(settings_preview PRIVATE src)
target_link_libraries(settings_preview PRIVATE ez_translate Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
set_target_properties(settings_preview PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
if(WIN32)
    add_custom_command(TARGET settings_preview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:settings_preview>")
endif()

# --- OCR bench (console) — đo det/rec cho dải cỡ hover ---
add_executable(ocr_bench tools/ocr_bench.cpp)
target_link_libraries(ocr_bench PRIVATE ez_ocr Qt6::Gui)
set_target_properties(ocr_bench PROPERTIES WIN32_EXECUTABLE OFF)
if(WIN32)
    add_custom_command(TARGET ocr_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:ocr_bench>")
endif()

# --- OCR model evaluation harness: image + sidecar .txt -> CSV CER/exact/timing ---
add_executable(ocr_model_eval tools/ocr_model_eval.cpp)
target_link_libraries(ocr_model_eval PRIVATE ez_ocr Qt6::Core Qt6::Gui)
set_target_properties(ocr_model_eval PROPERTIES WIN32_EXECUTABLE OFF)
if(WIN32)
    add_custom_command(TARGET ocr_model_eval POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:ocr_model_eval>")
endif()

# --- Scroll cache bench (console) — đo "cuộn 1 dòng" trên engine dịch thật ---
add_executable(scroll_cache_bench tools/scroll_cache_bench.cpp)
target_link_libraries(scroll_cache_bench PRIVATE ez_translate)
set_target_properties(scroll_cache_bench PROPERTIES WIN32_EXECUTABLE OFF)

# --- Plate bench (console) — đo thời gian dựng smart-cover plate ---
add_executable(plate_bench
    tools/plate_bench.cpp
    src/overlay/TextCover.h
    src/overlay/TextCover.cpp
)
target_include_directories(plate_bench PRIVATE src)
target_link_libraries(plate_bench PRIVATE Qt6::Gui)
set_target_properties(plate_bench PROPERTIES WIN32_EXECUTABLE OFF)

# --- Overlay probe (console) — chạy pipeline thật trên ẢNH và render overlay ra PNG để soi ---
add_executable(overlay_probe
    tools/overlay_probe.cpp
    src/overlay/TextOverlay.h
    src/overlay/TextOverlay.cpp
    src/overlay/OverlayLayout.h
    src/overlay/OverlayLayout.cpp
    src/overlay/ParagraphAnchor.h
    src/overlay/ParagraphAnchor.cpp
    src/overlay/TextFlow.h
    src/overlay/TextFlow.cpp
    src/platform/PlatformUtils.h
    src/platform/PlatformUtils.cpp
)
target_include_directories(overlay_probe PRIVATE src)
target_link_libraries(overlay_probe PRIVATE ez_ocr ez_translate Qt6::Gui Qt6::Widgets)
set_target_properties(overlay_probe PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
if(WIN32)
    add_custom_command(TARGET overlay_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:overlay_probe>")
endif()

# --- Hover probe test (console) — chứng minh thuật toán gom ĐOẠN, không cần UI ---
add_executable(hover_probe_test tools/hover_probe_test.cpp)
target_link_libraries(hover_probe_test PRIVATE ez_ocr Qt6::Gui)
set_target_properties(hover_probe_test PROPERTIES WIN32_EXECUTABLE OFF)
if(WIN32)
    add_custom_command(TARGET hover_probe_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:hover_probe_test>")
endif()

# --- Frame noise probe (console) — đo nhiễu khung THẬT của đường capture WGC ---
# Quyết định việc cache box so hash TUYỆT ĐỐI có trượt mỗi khung (churn) hay không.
add_executable(frame_noise_probe
    tools/frame_noise_probe.cpp
    src/capture/IWindowCapture.h
    src/capture/GdiWindowCapture.h
    src/capture/GdiWindowCapture.cpp
    src/capture/WgcSharedMemory.h
    src/capture/WgcWindowCapture.h
    src/capture/WgcWindowCapture.cpp
)
target_include_directories(frame_noise_probe PRIVATE src)
target_link_libraries(frame_noise_probe PRIVATE Qt6::Gui user32 gdi32)
set_target_properties(frame_noise_probe PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)

# --- Unit Tests ---
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    enable_testing()

    add_executable(test_ocr_mailbox
        tests/test_ocr_mailbox.cpp
        src/ocr/OcrRegionMailbox.h
        src/ocr/OcrRegionMailbox.cpp
    )
    target_include_directories(test_ocr_mailbox PRIVATE src)
    target_link_libraries(test_ocr_mailbox PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    set_target_properties(test_ocr_mailbox PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME test_ocr_mailbox COMMAND test_ocr_mailbox)

    add_executable(test_ocr_thread_budget
        tests/test_ocr_thread_budget.cpp
        src/ocr/OcrThreadBudget.h
        src/ocr/OcrThreadBudget.cpp
        src/translation/TranslationThreadBudget.h
        src/translation/TranslationThreadBudget.cpp
    )
    target_include_directories(test_ocr_thread_budget PRIVATE src)
    target_link_libraries(test_ocr_thread_budget PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    set_target_properties(test_ocr_thread_budget PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME test_ocr_thread_budget COMMAND test_ocr_thread_budget)

    add_executable(test_translation_qos
        tests/test_translation_qos.cpp
        src/translation/TranslationCircuitBreaker.h
        src/translation/TranslationCircuitBreaker.cpp
    )
    target_include_directories(test_translation_qos PRIVATE src)
    target_link_libraries(test_translation_qos PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(test_translation_qos PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME test_translation_qos COMMAND test_translation_qos)

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
        src/detection/ImageHash.h
        src/detection/TextBox.h
        src/detection/ITextDetector.h
        src/detection/HeuristicTextDetector.h
        src/detection/HeuristicTextDetector.cpp
        src/detection/RecursiveXYCut.h
        src/detection/RecursiveXYCut.cpp
        src/detection/ChangeDetector.h
        src/detection/ChangeDetector.cpp
        src/ocr/OcrScheduler.h
        src/ocr/OcrScheduler.cpp
    )
    target_include_directories(test_detection PRIVATE src)
    target_link_libraries(test_detection PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    set_target_properties(test_detection PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MINGW OR GCC)
        target_compile_options(test_detection PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()

    add_executable(test_overlay
        tests/test_overlay.cpp
        src/overlay/ParagraphAnchor.h
        src/overlay/ParagraphAnchor.cpp
        src/overlay/TextCover.h
        src/overlay/TextCover.cpp
        src/overlay/TextOverlay.h
        src/overlay/TextOverlay.cpp
        src/platform/PlatformUtils.h
        src/platform/PlatformUtils.cpp
    )
    target_include_directories(test_overlay PRIVATE src)
    target_link_libraries(test_overlay PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    set_target_properties(test_overlay PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MINGW OR GCC)
        target_compile_options(test_overlay PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()

    add_executable(test_translation
        tests/test_translation.cpp
        src/translation/ITranslator.h
        src/translation/TranslationCache.h
        src/translation/TranslationCache.cpp
        src/translation/TranslationPost.h
        src/translation/TranslationPost.cpp
        src/translation/LanguageDetect.h
        src/translation/LanguageDetect.cpp
        src/translation/DocumentTranslator.h
        src/translation/DocumentTranslator.cpp
    )
    target_include_directories(test_translation PRIVATE src)
    target_link_libraries(test_translation PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(test_translation PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MINGW OR GCC)
        target_compile_options(test_translation PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
endif()
