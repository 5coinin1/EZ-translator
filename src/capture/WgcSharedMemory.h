#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Bố cục bộ nhớ chia sẻ giữa helper process (Windows.Graphics.Capture)
 *        và app chính.
 *
 * THUẦN C++ — không dùng Qt, không dùng Windows header, để cả hai phía đều
 * include được. Việc đồng bộ (memory barrier / interlocked) do phía .cpp lo.
 *
 * Sơ đồ:
 *   [ControlBlock]                 (app chính tạo)  – cờ ready/stop/status
 *   [FrameHeader][slot0][slot1]    (helper tạo)     – double-buffer frame
 *
 * Double buffer chống tearing: helper ghi vào slot KHÔNG phải slot đang được
 * công bố, rồi mới tăng `sequence` và đổi `slot`. App chính chỉ đọc slot đã
 * được công bố.
 */
namespace EZTranslator::wgc {

inline constexpr uint32_t kMagic   = 0x455A5747; // 'EZWC'
inline constexpr uint32_t kVersion = 1;
inline constexpr int      kSlotCount = 2;        // double buffer

/** Vùng điều khiển nhỏ. App chính tạo, helper mở. */
struct ControlBlock
{
    uint32_t magic{0};
    uint32_t version{0};
    int32_t  ready{0};     ///< 0 = đang chờ, 1 = sẵn sàng, -1 = lỗi
    int32_t  status{0};    ///< mã trạng thái helper ghi (HelperStatus)
    int32_t  stop{0};      ///< app chính -> helper: 1 = yêu cầu dừng
    int32_t  reserved{0};
};

/** Header của vùng dữ liệu. Helper tạo, app chính mở. */
struct FrameHeader
{
    uint32_t magic{0};
    uint32_t version{0};
    int32_t  slotCount{0};
    int32_t  slotBytes{0};
    int64_t  sequence{0};  ///< tăng sau mỗi frame được commit
    int32_t  slot{0};      ///< slot vừa commit (0..slotCount-1)
    int32_t  width{0};
    int32_t  height{0};
    int32_t  stride{0};    ///< byte mỗi hàng
    int32_t  reserved{0};
};

/** Mã trạng thái helper báo về ControlBlock::status. */
enum class HelperStatus : int32_t
{
    Ok                = 0,
    BadArguments      = 1,
    ControlOpenFailed = 2,
    CaptureStartFailed= 3,
    DataCreateFailed  = 4,
    WindowClosed      = 5,
};

/** Kích thước (byte) của vùng dữ liệu với slot đã cho. */
[[nodiscard]] inline size_t dataSectionBytes(int width, int height)
{
    const size_t row = size_t(width) * 4u;
    size_t slot = row * size_t(height);
    slot = (slot + 63u) & ~size_t(63u); // căn 64 byte
    return sizeof(FrameHeader) + size_t(kSlotCount) * slot;
}

/** Kích thước mỗi slot (đã căn) với kích thước ảnh cho trước. */
[[nodiscard]] inline int slotBytesFor(int width, int height)
{
    size_t slot = size_t(width) * 4u * size_t(height);
    slot = (slot + 63u) & ~size_t(63u);
    return int(slot);
}

} // namespace EZTranslator::wgc
