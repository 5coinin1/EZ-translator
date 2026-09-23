#include "ocr/OcrTextAssembler.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace EZTranslator {
namespace OcrTextAssembler {

namespace {

int medianHeight(const QList<OcrTextBox>& boxes)
{
    if (boxes.isEmpty())
        return 0;
    QList<int> heights;
    heights.reserve(boxes.size());
    for (const OcrTextBox& box : boxes)
        heights.append(box.rect.height());
    std::sort(heights.begin(), heights.end());
    return heights.at(heights.size() / 2);
}

struct Line
{
    QList<OcrTextBox> boxes;
    double centerY{0.0};
    int count{0};
};

/** Nhóm box thành dòng (tâm y gần nhau trong dung sai theo chiều cao trung vị). */
QList<Line> groupLines(const QList<OcrTextBox>& boxes)
{
    QList<Line> lines;
    if (boxes.isEmpty())
        return lines;

    const int median = std::max(1, medianHeight(boxes));
    const double lineTolerance = median * 0.6;

    QList<OcrTextBox> sorted = boxes;
    std::sort(sorted.begin(), sorted.end(), [](const OcrTextBox& a, const OcrTextBox& b) {
        if (a.rect.center().y() != b.rect.center().y())
            return a.rect.center().y() < b.rect.center().y();
        return a.rect.left() < b.rect.left();
    });

    for (const OcrTextBox& box : sorted) {
        const double centerY = box.rect.center().y();
        int match = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (std::abs(lines[i].centerY - centerY) <= lineTolerance) {
                match = i;
                break;
            }
        }
        if (match < 0) {
            Line line;
            line.boxes.append(box);
            line.centerY = centerY;
            line.count = 1;
            lines.append(line);
        } else {
            Line& line = lines[match];
            line.centerY = (line.centerY * line.count + centerY) / (line.count + 1);
            ++line.count;
            line.boxes.append(box);
        }
    }

    std::sort(lines.begin(), lines.end(),
              [](const Line& a, const Line& b) { return a.centerY < b.centerY; });
    return lines;
}

int lineTopOf(const Line& line)
{
    int top = line.boxes.first().rect.top();
    for (const OcrTextBox& box : line.boxes)
        top = std::min(top, box.rect.top());
    return top;
}

int lineBottomOf(const Line& line)
{
    int bottom = line.boxes.first().rect.bottom();
    for (const OcrTextBox& box : line.boxes)
        bottom = std::max(bottom, box.rect.bottom());
    return bottom;
}

int lineLeftOf(const Line& line)
{
    int left = line.boxes.first().rect.left();
    for (const OcrTextBox& box : line.boxes)
        left = std::min(left, box.rect.left());
    return left;
}

QList<int> linePitches(const QList<Line>& lines)
{
    QList<int> pitches;
    pitches.reserve(lines.size() - 1);
    for (int i = 1; i < lines.size(); ++i)
        pitches.append(lineTopOf(lines.at(i)) - lineTopOf(lines.at(i - 1)));
    return pitches;
}

/** Trung vị dưới: nhịp chuẩn là nhịp phổ biến nhất, khe đoạn không được kéo nó lên. */
int lowerMedian(QList<int> values)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at((values.size() - 1) / 2);
}

double paragraphRatio(const QList<int>& pitches)
{
    const int base = lowerMedian(pitches);
    if (base <= 0)
        return 0.0;
    int best = 0;
    for (const int pitch : pitches) {
        if (pitch > base * 1.25 && (best == 0 || pitch < best))
            best = pitch;
    }
    return best > 0 ? double(best) / double(base) : 0.0;
}

// Model thường thêm dấu chấm vào cuối dòng bị NGẮT (wrap) — dòng tiếp theo mở đầu
// bằng chữ thường nghĩa là câu chưa kết thúc, nên dấu chấm là giả và bị bỏ.
void repairLineEnds(QStringList& texts, const QList<bool>& paragraphBreaks)
{
    for (int i = 0; i < texts.size(); ++i) {
        QString& text = texts[i];
        text = text.trimmed();
        if (text.endsWith(QStringLiteral("..")) && !text.endsWith(QStringLiteral("...")))
            text.chop(1);

        int dots = 0;
        while (dots < text.size() && text.at(text.size() - 1 - dots) == QLatin1Char('.'))
            ++dots;
        if (dots != 1 || i + 1 >= texts.size() || paragraphBreaks.value(i + 1, false))
            continue;
        const QString next = texts.at(i + 1).trimmed();
        if (next.isEmpty() || !next.at(0).isLower())
            continue;
        const QString chopped = text.left(text.size() - 1).trimmed();
        if (!chopped.isEmpty())
            text = chopped;
    }
}

struct BuiltDocument
{
    QString text;
    QList<LineText> lines;
};

BuiltDocument buildDocument(const QList<OcrTextBox>& boxes, int basePitchHint,
                            double paragraphRatioHint)
{
    BuiltDocument built;
    if (boxes.isEmpty())
        return built;

    const int median = std::max(1, medianHeight(boxes));
    const double spaceFactor = 0.4;

    QList<Line> lines = groupLines(boxes);

    QList<int> pitches;
    if (lines.size() >= 2)
        pitches = linePitches(lines);

    int basePitch = basePitchHint;
    if (basePitch <= 0) {
        if (pitches.size() >= 2)
            basePitch = lowerMedian(pitches);
        if (basePitch <= 0)
            basePitch = std::max(1, median);
    }

    double ratio = paragraphRatioHint;
    if (ratio <= 0.0)
        ratio = paragraphRatio(pitches);

    const int minGap = basePitch + std::max(3, median / 3);
    int paragraphGap = 0;
    if (ratio > 0.0)
        paragraphGap = std::max(minGap, int(std::lround(basePitch * ratio)));
    else if (pitches.size() < 2)
        paragraphGap = minGap;

    // Lề trái = dòng bắt đầu nhỏ nhất. Dòng bắt đầu lệch phải rõ rệt là đoạn thụt
    // đầu dòng (nhiều game dùng thụt lề thay vì dòng trống).
    int margin = 0;
    for (int i = 0; i < lines.size(); ++i)
        margin = i == 0 ? lineLeftOf(lines.at(i)) : std::min(margin, lineLeftOf(lines.at(i)));
    const int indentThreshold = std::max(5, int(median * 2.0 / 3.0));

    QStringList lineTexts;
    QList<bool> paragraphBreaks; // ngắt TRƯỚC dòng i
    QList<QRect> lineBoxes;
    lineTexts.reserve(lines.size());
    paragraphBreaks.reserve(lines.size());
    lineBoxes.reserve(lines.size());

    for (int i = 0; i < lines.size(); ++i) {
        Line& line = lines[i];
        std::sort(line.boxes.begin(), line.boxes.end(),
                  [](const OcrTextBox& a, const OcrTextBox& b) {
                      return a.rect.left() < b.rect.left();
                  });

        bool paragraph = false;
        if (i > 0) {
            const int pitch = lineTopOf(line) - lineTopOf(lines.at(i - 1));
            paragraph = paragraphGap > 0 && pitch >= paragraphGap;

            const int prevLeft = lineLeftOf(lines.at(i - 1));
            const int currLeft = lineLeftOf(line);
            const bool prevAtMargin = prevLeft <= margin + indentThreshold / 2;
            const bool currIndented = currLeft >= margin + indentThreshold;
            if (!paragraph && prevAtMargin && currIndented)
                paragraph = true;
        }
        paragraphBreaks.append(paragraph);

        QString text;
        QRect lineBox;
        int previousRight = -1;
        for (const OcrTextBox& box : line.boxes) {
            if (previousRight >= 0) {
                const int gap = box.rect.left() - previousRight;
                if (gap > spaceFactor * median)
                    text += QLatin1Char(' ');
            }
            text += box.text;
            lineBox = lineBox.isNull() ? box.rect : lineBox.united(box.rect);
            previousRight = box.rect.right();
        }
        lineTexts.append(text);
        lineBoxes.append(lineBox);
    }

    repairLineEnds(lineTexts, paragraphBreaks);

    for (int i = 0; i < lineTexts.size(); ++i) {
        if (i > 0)
            built.text += paragraphBreaks.at(i) ? QStringLiteral("\n\n") : QStringLiteral("\n");
        built.text += lineTexts.at(i);
    }

    built.text = repairApostropheSpacing(built.text);

    const QStringList reparsed = built.text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    built.lines.reserve(lineBoxes.size());
    for (int i = 0, box = 0; i < reparsed.size() && box < lineBoxes.size(); ++i) {
        if (reparsed.at(i).isEmpty())
            continue;
        LineText entry;
        entry.text = reparsed.at(i);
        entry.rect = lineBoxes.at(box++);
        built.lines.append(entry);
    }
    return built;
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber();
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019 /* ’ */);
}

/** True khi dấu nháy tại `index` thuộc một contraction thật (phải giữ lại). */
bool isContraction(const QString& text, int index)
{
    if (index <= 0 || index + 1 > text.size())
        return false;

    const QChar left = text.at(index - 1);
    if (!left.isLetter())
        return false;

    int end = index + 1;
    while (end < text.size() && isWordChar(text.at(end)))
        ++end;
    const QString right = text.mid(index + 1, end - index - 1);

    if (right.isEmpty()) // sở hữu cách: "lovers'"
        return true;

    static const QStringList keep = {QStringLiteral("s"), QStringLiteral("re"),
                                     QStringLiteral("ve"), QStringLiteral("ll"),
                                     QStringLiteral("m"), QStringLiteral("d")};
    const QString lower = right.toLower();
    if (keep.contains(lower))
        return true;

    if (lower == QStringLiteral("t") && left.toLower() == QLatin1Char('n')) // n't
        return true;

    return false;
}

} // namespace

QList<QRect> lineRects(const QList<OcrTextBox>& boxes)
{
    QList<QRect> rects;
    for (const Line& line : groupLines(boxes)) {
        QRect rect;
        for (const OcrTextBox& box : line.boxes)
            rect = rect.isNull() ? box.rect : rect.united(box.rect);
        if (!rect.isEmpty())
            rects.append(rect);
    }
    return rects;
}

int estimateLinePitch(const QList<OcrTextBox>& boxes)
{
    const QList<Line> lines = groupLines(boxes);
    if (lines.size() < 3)
        return 0;
    return lowerMedian(linePitches(lines));
}

double estimateParagraphRatio(const QList<OcrTextBox>& boxes)
{
    const QList<Line> lines = groupLines(boxes);
    if (lines.size() < 2)
        return 0.0;
    return paragraphRatio(linePitches(lines));
}

QString assemble(const QList<OcrTextBox>& boxes, int basePitchHint, double paragraphRatioHint)
{
    return buildDocument(boxes, basePitchHint, paragraphRatioHint).text;
}

QList<LineText> assembleLines(const QList<OcrTextBox>& boxes, int basePitchHint)
{
    if (boxes.isEmpty())
        return {};
    return buildDocument(boxes, basePitchHint, 0.0).lines;
}

Assembled assembleDocument(const QList<OcrTextBox>& boxes, int basePitchHint,
                           double paragraphRatioHint)
{
    const BuiltDocument built = buildDocument(boxes, basePitchHint, paragraphRatioHint);
    Assembled assembled;
    assembled.text = built.text;
    assembled.lines = built.lines;
    return assembled;
}

QString repairApostropheSpacing(const QString& text)
{
    QList<int> convertible;
    for (int i = 0; i < text.size(); ++i) {
        if (isApostrophe(text.at(i)) && !isContraction(text, i))
            convertible.append(i);
    }

    // Một dấu nháy đơn lẻ có thể là trích dẫn thật; artifact thể hiện vài dấu mỗi dòng.
    if (convertible.size() < 2)
        return text;

    QString repaired = text;
    for (const int index : convertible)
        repaired[index] = QLatin1Char(' ');
    return repaired;
}

} // namespace OcrTextAssembler
} // namespace EZTranslator
