#pragma once

#include <QPoint>
#include <QRect>

#include <algorithm>

namespace EZTranslator {

inline QRect hoverSelectionEndpointRect(const QPoint& start, const QPoint& end)
{
    return QRect(QPoint(std::min(start.x(), end.x()), std::min(start.y(), end.y())),
                 QPoint(std::max(start.x(), end.x()), std::max(start.y(), end.y())));
}

/** Vùng cần OCR từ nét bôi chữ. Với nhiều dòng, hai đầu nét kéo không phải
 *  hai góc của đoạn văn: các dòng ở giữa kéo dài tới mép cột. */
inline QRect hoverSelectionReadRect(const QPoint& start, const QPoint& end,
                                    const QRect& client)
{
    if (client.isEmpty())
        return {};
    const QRect endpoints = hoverSelectionEndpointRect(start, end).intersected(client);
    if (endpoints.isEmpty())
        return {};

    const bool multipleLines = std::abs(start.y() - end.y()) > 32;
    const int verticalMargin = multipleLines ? 20 : 18;
    if (!multipleLines)
        return endpoints.adjusted(-6, -verticalMargin, 6, verticalMargin).intersected(client);

    // Lấy cả bề rộng cột văn bản quanh điểm bắt đầu/kết thúc. 1600 px giữ đủ
    // dòng dài của PDF/game 2K mà không phải OCR toàn cửa sổ 2880 px.
    const int width = std::min(client.width(), std::max(1600, endpoints.width() + 500));
    const int left = std::clamp(endpoints.left() - 400, client.left(),
                                client.right() + 1 - width);
    return QRect(left, endpoints.top() - verticalMargin, width,
                 endpoints.height() + 2 * verticalMargin).intersected(client);
}

} // namespace EZTranslator
