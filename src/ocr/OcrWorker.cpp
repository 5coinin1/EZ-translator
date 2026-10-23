#include "ocr/OcrWorker.h"

#include "detection/ImageHash.h"
#include "ocr/OcrDetector.h"
#include "ocr/OcrRecognizer.h"
#include "ocr/OcrTextAssembler.h"
#include "overlay/TextCover.h"

#include <QThread>
#include <QVector>
#include <QDebug>

#include <algorithm>
#include <climits>

namespace EZTranslator {

namespace {

/** Khoảng cách từ `value` tới đoạn [low, high] (0 nếu nằm trong). */
int distanceToSpan(int value, int low, int high)
{
    if (value < low)
        return low - value;
    if (value > high)
        return value - high;
    return 0;
}

/**
 * @brief Luma trung vị của vành ngoài crop = màu nền.
 *
 * Detector nở box ra ngoài glyph nên viền crop là màu nền (panel) bất kể theme.
 * Không dùng ngưỡng Otsu: panel phẳng (UI game) làm phương sai giữa hai lớp bằng
 * nhau trên cả khe nên ngưỡng rơi vào nền và "khe" biến mất.
 */
int borderLuma(const QImage& gray, int margin)
{
    const int width = gray.width();
    const int height = gray.height();
    const int band = qBound(1, margin, qMax(1, qMin(width, height) / 2));

    QVector<int> values;
    values.reserve(size_t(width) * size_t(band) * 2 + size_t(height) * size_t(band) * 2);
    for (int y = 0; y < height; ++y) {
        const uchar* line = gray.constScanLine(y);
        const bool edgeRow = (y < band || y >= height - band);
        for (int x = 0; x < width; ++x) {
            if (edgeRow || x < band || x >= width - band)
                values.append(int(line[x]));
        }
    }
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

/**
 * @brief Cắt một box rộng thành các mảnh theo cột "hết mực".
 *
 * Một box det có thể phủ nhiều văn bản riêng biệt trên cùng hàng (thanh tab game):
 * đọc cả box sẽ ra "排位赛 挑战赛 排行榜" và dịch một câu không ai hỏi. Cắt tại khe
 * mực rồi chỉ đọc mảnh con trỏ đang chỉ.
 */
QList<QRect> splitOnInkGaps(const QImage& image, const QRect& region, int minGap)
{
    const QRect bounded = region.intersected(image.rect());
    if (bounded.isEmpty())
        return {};
    if (bounded.width() < 8 || bounded.height() < 4)
        return {bounded};

    const QImage gray = image.copy(bounded).convertToFormat(QImage::Format_Grayscale8);
    const int width = gray.width();
    const int height = gray.height();

    const int background = borderLuma(gray, 1);
    const auto isInk = [background](int value) { return qAbs(value - background) > 40; };

    QVector<int> columnInk(width, 0);
    for (int x = 0; x < width; ++x) {
        int count = 0;
        for (int y = 0; y < height; ++y)
            if (isInk(int(gray.constScanLine(y)[x])))
                ++count;
        columnInk[x] = count;
    }

    // Cột "rỗng" khi gần như không có mực; khử răng cưa để lại vài điểm ảnh lẻ.
    const int minInk = qMax(1, height / 20);
    const int gap = qMax(2, minGap);
    // Vạch kẻ mỏng giữa hai panel là "mực" vài cột, không được thành mảnh riêng:
    // chữ không bao giờ hẹp thế so với chiều cao dòng.
    const int minPieceWidth = qMax(8, height / 3);

    QList<QRect> pieces;
    int start = -1;
    int emptyRun = 0;
    const auto flush = [&](int end) {
        if (start < 0 || end < start)
            return;
        const int pieceWidth = end - start + 1;
        if (pieceWidth >= minPieceWidth)
            pieces.append(QRect(bounded.x() + start, bounded.y(), pieceWidth, bounded.height()));
    };
    for (int x = 0; x < width; ++x) {
        if (columnInk[x] >= minInk) {
            if (start < 0)
                start = x;
            emptyRun = 0;
        } else if (start >= 0) {
            if (++emptyRun >= gap) {
                flush(x - emptyRun);
                start = -1;
                emptyRun = 0;
            }
        }
    }
    if (start >= 0)
        flush(width - 1);

    if (pieces.isEmpty())
        return {bounded};
    return pieces;
}

/**
 * @brief Nhóm các rect trên cùng hàng thành cụm theo khe ngang.
 *
 * Dùng chung quy tắc với layout: xem OcrTextAssembler::clusterRow().
 */
QList<QList<int>> clusterRow(const QList<QRect>& rects, int gapThreshold)
{
    return EZTranslator::OcrTextAssembler::clusterRow(rects, gapThreshold);
}

} // namespace

OcrWorker::OcrWorker(QObject* parent)
    : QObject(parent)
{
}

OcrWorker::~OcrWorker() = default;

void OcrWorker::warmUp()
{
    // Cùng cỡ dải với hover để shape det được biên dịch sẵn; rec làm nóng vài bề rộng.
    QImage dummy(1200, 96, QImage::Format_RGB32);
    dummy.fill(QColor(200, 200, 200));
    const auto detectedBoxes = m_engine.detector().detect(dummy);
    Q_UNUSED(detectedBoxes);

    for (int width : {160, 320, 640}) {
        EZTranslator::OcrTextBox box;
        box.rect = QRect(4, 20, width, 48);
        const auto recognizedBoxes =
            m_engine.recognizer().recognize(dummy, QList<EZTranslator::OcrTextBox>{box});
        Q_UNUSED(recognizedBoxes);
    }
}

void OcrWorker::load(const EZTranslator::OcrOptions& options)
{
    QString error;
    if (!m_engine.load(options, &error)) {
        emit loaded(false, QString(), error);
        return;
    }
    warmUp();
    emit loaded(true, m_engine.providerName(), QString());
}

void OcrWorker::recognizeRegion(const QString& id, const QImage& image)
{
    m_pendingRequests.fetch_sub(1);

    const QList<EZTranslator::OcrTextBox> boxes = m_engine.recognize(image);
    emit regionRecognized(id, boxes); // phát NGAY: assembly + gửi dịch chạy tiếp

    // Dựng plate (nền xoá chữ) trên CHÍNH luồng này, SAU khi đã phát box: plate 100-270ms KHÔNG
    // được nằm trên luồng UI — trước đây nó dựng ở đó và chặn mọi kết quả OCR/dịch phía sau
    // (log thật: OCR ghi "sau 663ms", lượt trúng cache cũng ghi "dịch 317ms" vì kết quả xếp hàng).
    // CHỈ dựng khi KHÔNG còn yêu cầu OCR nào đang xếp hàng (nội dung đã ổn định): lúc cuộn, bản
    // dựng cho khung cũ là vô ích mà còn làm kết quả OCR mới phải chờ sau nó.
    const bool moreRequestsPending = m_pendingRequests.load() > 0;
    QList<QRect> textRects;
    textRects.reserve(boxes.size());
    for (const EZTranslator::OcrTextBox& box : boxes) {
        if (!box.rect.isEmpty())
            textRects.append(box.rect);
    }
    if (!moreRequestsPending && !textRects.isEmpty()) {
        emit regionPlateReady(id, EZTranslator::imageDifferenceHash(image),
                              TextCover::cleanPlate(image, image.rect(), textRects));
    }
}

void OcrWorker::probeHover(const QString& id, const QImage& band, const QPoint& cursorLocal)
{
    // Giữ nguyên ĐỘ PHÂN GIẢI hiệu dụng khi dải rộng: det resize sao cho cạnh dài ≤
    // giới hạn, nên dải rộng gấp rưỡi mà giữ giới hạn 640 thì chữ nhỏ đi 1.5 lần và
    // det dễ hụt. Thang theo bề rộng dải (dải thường 1200 → vẫn đúng 640 như trước).
    const int limitSide = qBound(640, 640 * band.width() / 1200, 2048);

    // PHA 1 — chỉ DÒ dòng (detection rẻ), CHƯA nhận dạng chữ.
    const QList<EZTranslator::OcrTextBox> boxes = m_engine.detector().detect(band, limitSide);

    // Chọn box con trỏ đang chỉ: ưu tiên trục dọc rồi ngang (khoảng cách tới MÉP box).
    const EZTranslator::OcrTextBox* hit = nullptr;
    int hitVertical = INT_MAX;
    int hitHorizontal = INT_MAX;
    for (const EZTranslator::OcrTextBox& box : boxes) {
        const int vertical =
            distanceToSpan(cursorLocal.y(), box.rect.top(), box.rect.bottom());
        const int horizontal =
            distanceToSpan(cursorLocal.x(), box.rect.left(), box.rect.right());
        if (vertical < hitVertical
            || (vertical == hitVertical && horizontal < hitHorizontal)) {
            hitVertical = vertical;
            hitHorizontal = horizontal;
            hit = &box;
        }
    }

    QString text;
    QRect lineRect;

    // Khoan dung khi con trỏ nằm giữa hai dòng: bám dòng gần nhất thay vì im lặng.
    if (hit && hitVertical <= qMax(8, hit->rect.height())
        && hitHorizontal <= qMax(40, hit->rect.width())) {
        // Chỉ lấy CỤM chứa con trỏ trong hàng (tránh nối nhiều tab cách xa nhau).
        const int rowCenter = hit->rect.center().y();
        const int rowTolerance = qMax(6, hit->rect.height() / 2 + 2);
        QList<const EZTranslator::OcrTextBox*> row;
        for (const EZTranslator::OcrTextBox& box : boxes) {
            if (qAbs(box.rect.center().y() - rowCenter) <= rowTolerance)
                row.append(&box);
        }
        std::sort(row.begin(), row.end(), [](const auto* a, const auto* b) {
            return a->rect.left() < b->rect.left();
        });

        QList<QRect> rowRects;
        rowRects.reserve(row.size());
        for (const auto* box : row)
            rowRects.append(box->rect);

        const int gapThreshold = qMax(8, int(hit->rect.height() * 1.2));
        QList<QRect> chosen;
        const QList<QList<int>> clusters = clusterRow(rowRects, gapThreshold);
        for (const QList<int>& cluster : clusters) {
            bool holdsCursor = false;
            for (int index : cluster) {
                if (row.at(index) == hit) {
                    holdsCursor = true;
                    break;
                }
            }
            if (holdsCursor) {
                for (int index : cluster)
                    chosen.append(row.at(index)->rect);
                break;
            }
        }
        if (chosen.isEmpty())
            chosen.append(hit->rect);

        QRect unionRect;
        for (const QRect& rect : chosen)
            unionRect = unionRect.isNull() ? rect : unionRect.united(rect);

        // Một box có thể phủ nhiều văn bản trên cùng hàng → cắt theo khe mực và chỉ
        // đọc mảnh mà con trỏ đang chỉ.
        QList<QRect> toRead = chosen;
        lineRect = unionRect;
        const int minGap = qMax(8, unionRect.height() * 3 / 5);
        const QList<QRect> pieces = splitOnInkGaps(band, unionRect, minGap);
        if (pieces.size() > 1) {
            const QRect* piece = nullptr;
            int bestHorizontal = INT_MAX;
            for (const QRect& candidate : pieces) {
                const int horizontal =
                    distanceToSpan(cursorLocal.x(), candidate.left(), candidate.right());
                if (horizontal < bestHorizontal) {
                    bestHorizontal = horizontal;
                    piece = &candidate;
                }
            }
            if (piece && !piece->isEmpty()) {
                toRead = QList<QRect>{*piece};
                lineRect = *piece;
            }
        }

        // PHA 2 — chỉ NHẬN DẠNG phần đã chọn (thường 1 dòng), không đọc cả dải.
        QList<EZTranslator::OcrTextBox> input;
        input.reserve(toRead.size());
        for (const QRect& rect : toRead) {
            EZTranslator::OcrTextBox box;
            box.rect = rect;
            input.append(box);
        }
        const QList<EZTranslator::OcrTextBox> recognized =
            m_engine.recognizer().recognize(band, input);

        // SẮP LẠI theo thứ tự ĐỌC trước khi ghép: recognizer trả về theo thứ tự input, mà
        // input lại bị xáo theo cụm/đoạn — không sắp thì các câu bị đảo và bản dịch sai nghĩa.
        QList<const EZTranslator::OcrTextBox*> ordered;
        ordered.reserve(recognized.size());
        for (const EZTranslator::OcrTextBox& box : recognized) {
            if (!box.text.trimmed().isEmpty())
                ordered.append(&box);
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            if (a->rect.top() != b->rect.top())
                return a->rect.top() < b->rect.top();
            return a->rect.left() < b->rect.left();
        });

        // Cùng hàng → nối bằng dấu cách; khác hàng → xuống dòng (giữ cấu trúc đoạn).
        QStringList rows;
        int lastRowCenter = 0;
        int lastRowHeight = 1;
        for (const EZTranslator::OcrTextBox* box : ordered) {
            const int center = box->rect.center().y();
            if (rows.isEmpty() || qAbs(center - lastRowCenter) > qMax(6, lastRowHeight / 2)) {
                rows.append(box->text.trimmed());
                lastRowCenter = center;
                lastRowHeight = qMax(1, box->rect.height());
            } else {
                rows.last() += QLatin1Char(' ') + box->text.trimmed();
            }
        }

        QStringList cleaned;
        cleaned.reserve(rows.size());
        for (const QString& row : rows)
            cleaned.append(row.simplified());
        text = cleaned.join(QLatin1Char('\n'));

        if (text.isEmpty())
            lineRect = QRect();
        else
            m_hoverLineHeight = qMax(1, lineRect.height());
    }

    // QUAY VỀ BẢN CŨ: đơn vị = DÒNG con trỏ, dải cố định; không mở rộng vùng/đoạn nữa.
    qDebug() << "[Hover] dải" << band.width() << "x" << band.height() << "→ đọc"
             << lineRect.width() << "x" << lineRect.height() << text.left(40);

    emit hoverProbed(id, text, lineRect, false);
}

} // namespace EZTranslator
