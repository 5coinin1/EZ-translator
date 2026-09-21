#include "IconFactory.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace IconFactory
{

// ── Gamepad Icon ─────────────────────────────────────────────────────────────
QPixmap makeGamepadIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    float gw = size * 0.90f;
    float gh = size * 0.65f;
    float ox = (size - gw) * 0.5f;
    float oy = (size - gh) * 0.5f;

    // Silhouette thân controller theo Remix Icon / FontAwesome
    QPainterPath body;
    body.moveTo(ox + gw * 0.25f, oy);
    body.lineTo(ox + gw * 0.75f, oy);
    body.quadTo(ox + gw, oy + gh * 0.08f, ox + gw, oy + gh * 0.40f);
    body.quadTo(ox + gw * 0.96f, oy + gh * 0.82f, ox + gw * 0.85f, oy + gh);
    body.quadTo(ox + gw * 0.72f, oy + gh * 1.05f, ox + gw * 0.66f, oy + gh * 0.78f);
    body.quadTo(ox + gw * 0.60f, oy + gh * 0.55f, ox + gw * 0.50f, oy + gh * 0.55f);
    body.quadTo(ox + gw * 0.40f, oy + gh * 0.55f, ox + gw * 0.34f, oy + gh * 0.78f);
    body.quadTo(ox + gw * 0.28f, oy + gh * 1.05f, ox + gw * 0.15f, oy + gh);
    body.quadTo(ox + gw * 0.04f, oy + gh * 0.82f, ox, oy + gh * 0.40f);
    body.quadTo(ox, oy + gh * 0.08f, ox + gw * 0.25f, oy);
    body.closeSubpath();

    p.fillPath(body, color);

    // Cutouts: D-pad & 4 buttons (làm trong suốt để nền hiển thị qua)
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);

    // Left D-pad (+)
    float dpadX = ox + gw * 0.28f;
    float dpadY = oy + gh * 0.38f;
    float dpadArm = gw * 0.09f;
    float dpadThick = gw * 0.065f;
    p.drawRect(QRectF(dpadX - dpadArm, dpadY - dpadThick * 0.5f, dpadArm * 2.0f, dpadThick));
    p.drawRect(QRectF(dpadX - dpadThick * 0.5f, dpadY - dpadArm, dpadThick, dpadArm * 2.0f));

    // Right 4 buttons (diamond pattern)
    float btnX = ox + gw * 0.72f;
    float btnY = oy + gh * 0.38f;
    float btnSpacing = gw * 0.085f;
    float btnR = gw * 0.038f;

    p.drawEllipse(QPointF(btnX, btnY - btnSpacing), btnR, btnR); // top
    p.drawEllipse(QPointF(btnX, btnY + btnSpacing), btnR, btnR); // bottom
    p.drawEllipse(QPointF(btnX - btnSpacing, btnY), btnR, btnR); // left
    p.drawEllipse(QPointF(btnX + btnSpacing, btnY), btnR, btnR); // right

    return pix;
}

// ── Translate Icon (文A) ─────────────────────────────────────────────────────
QPixmap makeTranslateIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    float penW = qMax(1.6f, size * 0.082f);
    p.setPen(QPen(color, penW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    // ── Ký tự 文 (bên trái) ──
    float cX = size * 0.30f;
    float topY = size * 0.08f;

    // 1. Chấm trên đầu
    p.drawLine(QPointF(cX, topY), QPointF(cX, topY + size * 0.11f));

    // 2. Nét ngang
    float barY = topY + size * 0.15f;
    p.drawLine(QPointF(size * 0.08f, barY), QPointF(size * 0.54f, barY));

    // 3. Nét phẩy (xiên xuống trái)
    QPainterPath pie;
    pie.moveTo(cX, barY);
    pie.quadTo(cX - size * 0.05f, barY + size * 0.30f, size * 0.06f, size * 0.76f);
    p.drawPath(pie);

    // 4. Nét mác (xiên xuống phải)
    QPainterPath na;
    na.moveTo(cX - size * 0.06f, barY + size * 0.12f);
    na.quadTo(cX + size * 0.08f, barY + size * 0.32f, size * 0.56f, size * 0.76f);
    p.drawPath(na);

    // ── Ký tự A (bên phải, thấp hơn một chút) ──
    float aCx = size * 0.72f;
    float aTop = size * 0.32f;
    float aBottom = size * 0.90f;
    float aW = size * 0.32f;
    float aLeft = aCx - aW * 0.5f;
    float aRight = aCx + aW * 0.5f;

    // Chân trái & phải của A
    p.drawLine(QPointF(aCx, aTop), QPointF(aLeft, aBottom));
    p.drawLine(QPointF(aCx, aTop), QPointF(aRight, aBottom));

    // Gạch ngang giữa của A
    float crossY = aTop + (aBottom - aTop) * 0.58f;
    float crossLeft = aLeft + aW * 0.18f;
    float crossRight = aRight - aW * 0.18f;
    p.drawLine(QPointF(crossLeft, crossY), QPointF(crossRight, crossY));

    return pix;
}

// ── Profile / Document Icon ─────────────────────────────────────────────────
QPixmap makeProfileIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    float ox = size * 0.16f;
    float oy = size * 0.08f;
    float w  = size * 0.68f;
    float h  = size * 0.84f;
    float fold = w * 0.35f;
    float r = size * 0.08f;

    // Khung tài liệu với góc gấp trên phải
    QPainterPath doc;
    doc.moveTo(ox + r, oy);
    doc.lineTo(ox + w - fold, oy);
    doc.lineTo(ox + w, oy + fold);
    doc.lineTo(ox + w, oy + h - r);
    doc.quadTo(ox + w, oy + h, ox + w - r, oy + h);
    doc.lineTo(ox + r, oy + h);
    doc.quadTo(ox, oy + h, ox, oy + h - r);
    doc.lineTo(ox, oy + r);
    doc.quadTo(ox, oy, ox + r, oy);
    doc.closeSubpath();

    p.fillPath(doc, color);

    // Cắt góc gấp trên phải
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);

    QPainterPath foldCorner;
    foldCorner.moveTo(ox + w - fold, oy);
    foldCorner.lineTo(ox + w, oy);
    foldCorner.lineTo(ox + w, oy + fold);
    foldCorner.closeSubpath();
    p.fillPath(foldCorner, Qt::black);

    // Vẽ nếp gấp sáng màu
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QColor foldColor = color.lighter(135);
    QPainterPath flap;
    flap.moveTo(ox + w - fold, oy);
    flap.lineTo(ox + w - fold, oy + fold);
    flap.lineTo(ox + w, oy + fold);
    flap.closeSubpath();
    p.fillPath(flap, foldColor);

    // Cắt các dòng text bên trong
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setBrush(Qt::black);
    float lineH = h * 0.08f;
    float lx = ox + w * 0.22f;
    float ly1 = oy + h * 0.38f;
    float ly2 = oy + h * 0.56f;
    float ly3 = oy + h * 0.74f;

    p.drawRoundedRect(QRectF(lx, ly1, w * 0.38f, lineH), lineH * 0.5f, lineH * 0.5f);
    p.drawRoundedRect(QRectF(lx, ly2, w * 0.55f, lineH), lineH * 0.5f, lineH * 0.5f);
    p.drawRoundedRect(QRectF(lx, ly3, w * 0.30f, lineH), lineH * 0.5f, lineH * 0.5f);

    return pix;
}

// ── Crop / Viewfinder Icon [ ] ───────────────────────────────────────────────
QPixmap makeCropIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    float penW = qMax(2.0f, size * 0.11f);
    p.setPen(QPen(color, penW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    float m = size * 0.12f;
    float w = size - m * 2.0f;
    float h = size - m * 2.0f;
    float arm = w * 0.36f;

    // Góc trên-trái ┌
    QPainterPath tl;
    tl.moveTo(m, m + arm);
    tl.lineTo(m, m);
    tl.lineTo(m + arm, m);
    p.drawPath(tl);

    // Góc trên-phải ┐
    QPainterPath tr;
    tr.moveTo(m + w - arm, m);
    tr.lineTo(m + w, m);
    tr.lineTo(m + w, m + arm);
    p.drawPath(tr);

    // Góc dưới-phải ┘
    QPainterPath br;
    br.moveTo(m + w, m + h - arm);
    br.lineTo(m + w, m + h);
    br.lineTo(m + w - arm, m + h);
    p.drawPath(br);

    // Góc dưới-trái └
    QPainterPath bl;
    bl.moveTo(m + arm, m + h);
    bl.lineTo(m, m + h);
    bl.lineTo(m, m + h - arm);
    p.drawPath(bl);

    return pix;
}

// ── Play Triangle Icon ▶ ────────────────────────────────────────────────────
QPixmap makePlayIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    float ox = size * 0.28f;
    float oy = size * 0.22f;
    float w  = size * 0.52f;
    float h  = size * 0.56f;

    QPainterPath tri;
    tri.moveTo(ox, oy);
    tri.lineTo(ox + w, oy + h * 0.5f);
    tri.lineTo(ox, oy + h);
    tri.closeSubpath();

    p.fillPath(tri, color);
    return pix;
}

// ── Cờ Việt Nam 🇻🇳 ────────────────────────────────────────────────────────
QPixmap makeVietnamFlag(int width, int height)
{
    QPixmap pix(width, height);
    pix.fill(QColor("#da251d")); // Màu đỏ cờ Việt Nam
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Ngôi sao vàng 5 cánh ở trung tâm
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffff00"));

    float cx = width * 0.5f;
    float cy = height * 0.5f;
    float rOuter = height * 0.33f;
    float rInner = rOuter * 0.382f;

    QPainterPath star;
    for (int i = 0; i < 10; ++i) {
        float angle = -3.14159265f * 0.5f + i * 3.14159265f * 0.2f;
        float r = (i % 2 == 0) ? rOuter : rInner;
        float x = cx + r * cosf(angle);
        float y = cy + r * sinf(angle);
        if (i == 0) star.moveTo(x, y);
        else star.lineTo(x, y);
    }
    star.closeSubpath();
    p.fillPath(star, QColor("#ffff00"));

    return pix;
}

// ── Chevron Down ∨ ──────────────────────────────────────────────────────────
QPixmap makeChevronDownIcon(int size, QColor color)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(QPen(color, 2.0f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    float m = size * 0.20f;
    float w = size - m * 2.0f;
    float h = size - m * 2.0f;

    QPainterPath chevron;
    chevron.moveTo(m, m + h * 0.32f);
    chevron.lineTo(m + w * 0.5f, m + h * 0.78f);
    chevron.lineTo(m + w, m + h * 0.32f);
    p.drawPath(chevron);

    return pix;
}

// ── Game Thumbnail ──────────────────────────────────────────────────────────
QPixmap makeGameThumbnailIcon(int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Bo tròn góc
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), 4, 4);
    p.setClipPath(clip);

    // Nền tối sẫm
    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0, QColor("#0f172a"));
    bg.setColorAt(1, QColor("#1e1b4b"));
    p.fillRect(QRect(0, 0, size, size), bg);

    // Biểu tượng Elden Ring vàng ánh kim
    p.setPen(QPen(QColor("#facc15"), 1.2f, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(size * 0.5f, size * 0.45f), size * 0.25f, size * 0.25f);
    p.drawLine(QPointF(size * 0.5f, size * 0.15f), QPointF(size * 0.5f, size * 0.85f));
    p.drawArc(QRectF(size * 0.20f, size * 0.40f, size * 0.60f, size * 0.40f), 0, 180 * 16);

    return pix;
}

} // namespace IconFactory
