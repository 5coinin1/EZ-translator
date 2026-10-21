#pragma once

#include <QDialog>

#include "core/Types.h"

class QPlainTextEdit;
class QLabel;
class QCloseEvent;

/**
 * @brief Cửa sổ nổi hiển thị văn bản OCR và bản dịch.
 *
 * Đây là cửa sổ riêng (always-on-top), KHÔNG phải overlay trong suốt và
 * KHÔNG phải popup dịch khi chỉ chuột. Có thể kéo/thu nhỏ/đóng như cửa sổ
 * Windows bình thường.
 */
class FloatingTranslationWindow : public QDialog
{
    Q_OBJECT
public:
    explicit FloatingTranslationWindow(QWidget* parent = nullptr);

    void setSourceText(const QString& text);
    void setTranslatedText(const QString& text);
    void setStatusText(const QString& text);
    void setAppearance(const EZTranslator::AppSettings& settings);

signals:
    void closedByUser();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void applyAppearance();

    QLabel*         m_sourceLabel{nullptr};
    QLabel*         m_translatedLabel{nullptr};
    QPlainTextEdit* m_sourceText{nullptr};
    QPlainTextEdit* m_translatedText{nullptr};
    EZTranslator::AppSettings m_appearance;
};
