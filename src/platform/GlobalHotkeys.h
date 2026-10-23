#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QStringList>

/**
 * @brief Phím tắt TOÀN CỤC (hoạt động cả khi cửa sổ game đang ở tiền cảnh).
 *
 * Trước đây phím tắt là QShortcut gắn vào MainWindow — chỉ ăn khi cửa sổ app
 * đang focus, nên "F8 để dừng dịch" trong lúc chơi game gần như vô dụng. Lớp này
 * dùng Win32 RegisterHotKey (đăng ký vào HWND của MainWindow) và bắt WM_HOTKEY
 * qua native event filter của Qt.
 *
 * `apply()` trả về danh sách id đăng ký THẤT BẠI (phím đã bị app khác chiếm,
 * hoặc tổ hợp không hợp lệ) để caller cài QShortcut dự phòng.
 */
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    struct Binding
    {
        QString id;            ///< khoá nội bộ: "toggle", "selectRegion", …
        QKeySequence sequence; ///< rỗng = người dùng chưa gán → bỏ qua, không tính lỗi
    };

    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    /** Đăng ký lại toàn bộ (huỷ các đăng ký cũ trước). */
    QStringList apply(const QList<Binding>& bindings, quintptr ownerWindow);

    /** Huỷ toàn bộ đăng ký hiện có. */
    void clear();

    [[nodiscard]] static bool isSupported();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    /** Một phím tắt toàn cục vừa được bấm (id như trong Binding). */
    void activated(const QString& id);

private:
    QHash<int, QString> m_idByHotkeyId; ///< id số của Win32 → id nội bộ
};
