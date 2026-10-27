#pragma once

#include <QByteArray>
#include <QtGlobal>

class QWidget;

namespace EZTranslator::FramelessWindow {

/**
 * Trả lại khả năng resize bằng viền cho cửa sổ frameless.
 *
 * Cửa sổ dùng `Qt::FramelessWindowHint` không có viền của hệ điều hành nên
 * không thể kéo giãn bằng chuột. Hàm này xử lý `WM_NCHITTEST`: các vùng sát
 * viền (mặc định 6 DIP, góc rộng hơn) trả về mã resize tương ứng để Windows
 * tự lo kéo giãn và đổi con trỏ chuột đúng chuẩn.
 *
 * Việc kéo cửa sổ do thanh tiêu đề đảm nhiệm (TitleBar / DialogTitleBar dùng
 * QWindow::startSystemMove), không xử lý ở đây — nhờ vậy click vào nút bấm
 * trên thanh tiêu đề vẫn hoạt động bình thường.
 *
 * Cách dùng trong cửa sổ frameless:
 *
 *     bool MyWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result)
 *     {
 *         if (FramelessWindow::handleNativeMessage(this, type, message, result))
 *             return true;
 *         return QWidget::nativeEvent(type, message, result);
 *     }
 *
 * @param window          Cửa sổ frameless (top-level widget).
 * @param eventType       `eventType` nhận từ QWidget::nativeEvent.
 * @param message         `message` nhận từ QWidget::nativeEvent.
 * @param result          `result` nhận từ QWidget::nativeEvent.
 * @param resizeBorderDip Độ dày viền resize (DIP).
 * @return true nếu đã xử lý message (caller phải trả về true).
 */
bool handleNativeMessage(QWidget* window, const QByteArray& eventType, void* message,
                         qintptr* result, int resizeBorderDip = 6);

} // namespace EZTranslator::FramelessWindow
