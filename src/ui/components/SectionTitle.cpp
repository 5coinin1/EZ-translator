#include "SectionTitle.h"
#include "ui/theme/StyleTheme.h"
#include "ui/theme/IconFactory.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <QPixmap>

// ── Widget icon vẽ từ IconFactory ───────────────────────────────────────────
class VectorIconLabel : public QLabel
{
public:
    VectorIconLabel(SectionIconType type, int size, QWidget* parent = nullptr)
        : QLabel(parent)
    {
        QPixmap pix;
        switch (type) {
            case SectionIconType::Gamepad:
                pix = IconFactory::makeGamepadIcon(size, QColor("#cbd5e1"));
                break;
            case SectionIconType::Translate:
                pix = IconFactory::makeTranslateIcon(size, QColor("#cbd5e1"));
                break;
            case SectionIconType::Monitor:
                pix = IconFactory::makeMonitorIcon(size, QColor("#cbd5e1"));
                break;
            case SectionIconType::Crop:
                pix = IconFactory::makeCropIcon(size, QColor("#cbd5e1"));
                break;
            case SectionIconType::Profile:
                pix = IconFactory::makeProfileIcon(size, QColor("#60a5fa"));
                break;
        }
        setPixmap(pix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        setFixedSize(size, size);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// SectionTitle – constructor với SectionIconType (vector icon)
// ────────────────────────────────────────────────────────────────────────────
SectionTitle::SectionTitle(SectionIconType iconType, const QString& text, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 2);
    lay->setSpacing(8);

    auto* icon = new VectorIconLabel(iconType, 22, this);

    auto* textLabel = new QLabel(text, this);
    textLabel->setStyleSheet(QString("color: %1; font-size: 10pt; font-weight: 600;")
                                 .arg(StyleTheme::ColorTextPrimary));

    lay->addWidget(icon);
    lay->addWidget(textLabel);
    lay->addStretch();
}

// ────────────────────────────────────────────────────────────────────────────
// SectionTitle – constructor tương thích cũ (emoji string)
// ────────────────────────────────────────────────────────────────────────────
SectionTitle::SectionTitle(const QString& icon, const QString& text, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 2);
    lay->setSpacing(6);

    auto* iconLabel = new QLabel(icon, this);
    iconLabel->setStyleSheet(QString("color: %1; font-size: 12pt;")
                                 .arg(StyleTheme::ColorPrimary));
    iconLabel->setFixedWidth(22);

    auto* textLabel = new QLabel(text, this);
    textLabel->setStyleSheet(QString("color: %1; font-size: 10pt; font-weight: 600;")
                                 .arg(StyleTheme::ColorTextPrimary));

    lay->addWidget(iconLabel);
    lay->addWidget(textLabel);
    lay->addStretch();
}
