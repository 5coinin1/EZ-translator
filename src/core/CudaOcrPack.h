#pragma once

#include "core/Types.h"

#include <QString>

namespace EZTranslator::CudaOcrPack {

/**
 * @brief Vị trí gói CUDA OCR optional.
 *
 * Base app vẫn dùng ONNX Runtime DirectML. Nếu người dùng cài gói CUDA,
 * một profile app riêng nằm trong AppLocalData/cuda sẽ được tạo. Launcher ẩn
 * (main.cpp) chọn profile này khi setting OCR đang là cuda.
 */
QString profileDir();
QString profileExe();
QString markerPath();
QString envPath();
QString installScriptPath();

[[nodiscard]] bool isInstalled();
[[nodiscard]] bool requested(const AppSettings& settings);
[[nodiscard]] bool isProfileProcess();

/** Chép exe/helper/ct2_server hiện tại vào profile nếu bản trong profile cũ.
 *  Không đụng tới ONNX Runtime CUDA/CUDA DLLs đã cài. */
void refreshProfileAppFiles();

} // namespace EZTranslator::CudaOcrPack
