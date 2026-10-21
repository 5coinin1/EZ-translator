// Chạy ĐÚNG pipeline thật trên một ảnh chụp vùng (không cần cửa sổ): OCR → ghép dòng → dựng layout
// overlay (OverlayLayout — cùng code với app) → dựng plate (TextCover — như worker OCR) → RENDER
// bằng chính TextOverlay → lưu PNG để soi: chữ đặt đúng chỗ chưa, cover có phủ quá rộng không.
//
//   overlay_probe.exe <anh_vao.png> [anh_ra.png]
//   EZ_PROBE_TRANSLATE=1  → dịch thật qua ct2 (mặc định: hiện chính text nguồn, đủ để soi cover)
//
// Kết quả ghi thêm overlay_probe_report.txt (console môi trường này không hiện qInfo).
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTextStream>

#include "ocr/OcrEngine.h"
#include "ocr/OcrTextAssembler.h"
#include "overlay/OverlayLayout.h"
#include "overlay/TextCover.h"
#include "overlay/TextOverlay.h"
#include "translation/CTranslate2Translator.h"
#include "translation/DocumentTranslator.h"

using namespace EZTranslator;

namespace {

QTextStream* g_report = nullptr;

QString compactCjkSpaces(const QString& input)
{
    const auto isCjk = [](QChar c) {
        const ushort u = c.unicode();
        return (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
               || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x3040 && u <= 0x30FF)
               || (u >= 0xAC00 && u <= 0xD7AF);
    };
    QString out;
    out.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const QChar c = input.at(i);
        if (c.isSpace() && i > 0 && i + 1 < input.size()
            && isCjk(input.at(i - 1)) && isCjk(input.at(i + 1)))
            continue;
        out.append(c);
    }
    return out;
}

QString normalizeUiText(QString text)
{
    static const struct { const char* from; const char* to; } kReplacements[] = {
        {"Thập Tự Chuẩn Tinh", "tâm ngắm chữ thập"},
        {"Chuẩn Tinh", "tâm ngắm"},
        {"chuẩn tinh", "tâm ngắm"},
        {"记忆序列", "Chuỗi ký ức"},
        {"弦海信标", "Tín hiệu biển khúc"},
        {"活动凭录", "Chứng nhận sự kiện"},
        {"经验加成卡", "Thẻ tăng EXP"},
        {"3胜", "3 thắng"},
        {"已签到", "Đã điểm danh"},
        {"待签到", "Chờ điểm danh"},
        {"活动说明", "Hướng dẫn sự kiện"},
        {"活动中心", "Trung tâm sự kiện"},
        {"赛季限时", "Mùa giải giới hạn"},
        {"晶源感染", "Nhiễm nguồn tinh thể"},
        {"新人专属", "Độc quyền tân thủ"},
        {"聊天", "Trò chuyện"},
        {"返回", "Quay lại"},
        {"弦歌礼赠", "Quà tặng Khúc Ca"},
        {"Hạn dùng bộ tâm ngắm hình chữ thập", "Áp dụng tâm ngắm chữ thập"},
        {"Mở ra", "Bật"},
        {"Vây ngoài", "Viền ngoài"},
        {"Máy ngoài", "Viền ngoài"},
        {"Mấy ngoài", "Viền ngoài"},
        {"Linh Mẫn Độ", "Độ nhạy"},
        {"Thiết bị điều khiển", "Điều khiển"},
        {"Tài khoản hợp quy", "Tài khoản"},
        {"Đợi đến khi điểm danh", "Chờ điểm danh"},
        {"Hoạt động giải thích", "Hướng dẫn"},
        {"Thiết lập", "Cài đặt"},
        {"Đối chiến", "Đấu"},
        {"Cơ sở", "Cơ bản"},
        {"Thao tác", "Tác vụ"},
        {"Độ nhạy", "Nhạy"},
        {"Hình ảnh", "Hình"},
        {"Âm thanh", "Âm"},
    };
    for (const auto& replacement : kReplacements)
        text.replace(QString::fromUtf8(replacement.from), QString::fromUtf8(replacement.to));
    if (!text.isEmpty() && text.at(0).isLower())
        text[0] = text.at(0).toUpper();
    return text;
}

QString normalizeUiTabText(QString text)
{
    static const struct { const char* from; const char* to; } kReplacements[] = {
        {"Thiết bị điều khiển", "Điều"},
        {"Tài khoản hợp quy", "Tài khoản"},
        {"Thiết lập", "Cài đặt"},
        {"Đối chiến", "Đấu"},
        {"Cơ bản", "Cơ"},
        {"Cơ sở", "Cơ"},
        {"Thao tác", "Tác"},
        {"Tác vụ", "Tác"},
        {"Độ nhạy", "Nhạy"},
        {"Tâm ngắm", "Ngắm"},
        {"Chuẩn Tinh", "Ngắm"},
        {"Hình ảnh", "Hình"},
        {"Thanh âm", "Âm"},
        {"Âm thanh", "Âm"},
        {"Điều khiển", "Điều"},
        {"Trung tâm hoạt động", "Sự kiện"},
        {"Trung tâm sự kiện", "Sự kiện"},
        {"Giới hạn thời gian của mùa giải", "Mùa giải"},
        {"Mùa giải giới hạn", "Mùa giải"},
        {"Mập béo", "Cá béo"},
        {"Người mới độc quyền", "Tân thủ"},
        {"Người mới", "Tân thủ"},
        {"Độc quyền tân thủ", "Tân thủ"},
        {"Tính nguyên nhiễm", "Nhiễm"},
        {"Tinh nguyên nhiễm", "Nhiễm"},
        {"Nhiễm nguồn tinh thể", "Nhiễm"},
        {"Những", "Khác"},
    };
    for (const auto& replacement : kReplacements)
        text.replace(QString::fromUtf8(replacement.from), QString::fromUtf8(replacement.to));
    text = text.trimmed();
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    return text;
}

void reportLine(const QString& text)
{
    if (g_report)
        *g_report << text << "\n";
}

// Console môi trường này không hiện qInfo/qWarning → chuyển hết vào báo cáo để thấy log của engine
// (vd "[OCR] đã nạp model chữ dự phòng" / "nạp thất bại").
void reportHandler(QtMsgType, const QMessageLogContext&, const QString& message)
{
    reportLine(QStringLiteral("[qt] ") + message);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QFile reportFile(QStringLiteral("overlay_probe_report.txt"));
    reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream report(&reportFile);
    g_report = &report;
    qInstallMessageHandler(reportHandler);
    qInfo() << "[probe] bat dau (kiem tra handler log)";

    if (argc < 2) {
        reportLine(QStringLiteral("usage: overlay_probe.exe <anh_vao.png> [anh_ra.png]"));
        report.flush();
        return 2;
    }
    const QString inPath = QString::fromLocal8Bit(argv[1]);
    QImage image(inPath);
    if (image.isNull()) {
        reportLine(QStringLiteral("khong doc duoc anh: ") + inPath);
        report.flush();
        return 3;
    }
    reportLine(QStringLiteral("=== %1 (%2x%3) ===")
                   .arg(QFileInfo(inPath).fileName())
                   .arg(image.width())
                   .arg(image.height()));

    // ── OCR đúng model như app ───────────────────────────────────────────────
    const QString dir =
        qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));
    const QString sourceLang = qEnvironmentVariable("EZ_PROBE_SRC", QStringLiteral("en"));
    OcrOptions options;
    options.detModelPath = dir + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    if (sourceLang == QLatin1String("zh")) {
        options.recModelPath = dir + QStringLiteral("/ch_PP-OCRv4_rec_infer.onnx");
        options.dictionaryPath = dir + QStringLiteral("/ppocr_keys_v1.txt");
    } else if (sourceLang == QLatin1String("ja")) {
        options.recModelPath = dir + QStringLiteral("/japan_PP-OCRv4_rec_infer.onnx");
        options.dictionaryPath = dir + QStringLiteral("/japan_dict.txt");
    } else {
        options.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
        options.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    }
    options.useGpu = qEnvironmentVariable("EZ_OCR_USE_GPU", QStringLiteral("1"))
                     != QLatin1String("0");

    OcrEngine engine;
    QString error;
    QElapsedTimer timer;
    timer.start();
    if (!engine.load(options, &error)) {
        reportLine(QStringLiteral("nap OCR that bai: ") + error);
        report.flush();
        return 4;
    }
    const qint64 loadMs = timer.elapsed();

    timer.restart();
    const QList<OcrTextBox> boxes = engine.recognize(image);
    const qint64 ocrMs = timer.elapsed();
    double confidenceSum = 0.0;
    int confidenceCount = 0;
    for (const OcrTextBox& box : boxes) {
        if (box.text.trimmed().isEmpty())
            continue;
        confidenceSum += box.confidence;
        ++confidenceCount;
    }
    reportLine(QStringLiteral("OCR: %1 dong | load %2 ms | nhan dang %3 ms | tin cay TB %4")
                   .arg(boxes.size())
                   .arg(loadMs)
                   .arg(ocrMs)
                   .arg(confidenceCount > 0 ? confidenceSum / confidenceCount : 0.0, 0, 'f', 3));

    // ── Ghép dòng + tài liệu (bản dịch thật nếu bật) ─────────────────────────
    const OcrTextAssembler::Assembled assembled =
        OcrTextAssembler::assembleDocument(boxes, 0, 0.0);
    QList<QRect> lineRects;
    QStringList lineTexts;
    for (const OcrTextAssembler::LineText& line : assembled.lines) {
        lineRects.append(line.rect);
        lineTexts.append(line.text.simplified());
    }

    const bool probeUi = qEnvironmentVariableIsSet("EZ_PROBE_UI");
    QList<OcrTextBox> uiBoxes;
    QStringList uiTexts;
    if (probeUi) {
        for (const OcrTextBox& box : boxes) {
            const QString text = compactCjkSpaces(box.text.simplified());
            uiBoxes.append(box);
            if (!text.isEmpty())
                uiTexts.append(text);
        }
    }

    bool probeNovel = !probeUi
                      && (qEnvironmentVariableIsSet("EZ_PROBE_NOVEL")
                          || OverlayLayout::looksLikeNovel(lineRects, lineTexts, image.rect()));
    QList<OverlayLayout::NovelParagraph> novelParagraphs;
    QString novelText;
    if (probeNovel) {
        novelParagraphs = OverlayLayout::groupNovelParagraphs(lineRects, lineTexts);
        QStringList paragraphTexts;
        for (const auto& paragraph : novelParagraphs)
            paragraphTexts.append(paragraph.text.trimmed());
        novelText = paragraphTexts.join(QStringLiteral("\n\n"));
        probeNovel = novelParagraphs.size() >= 2 && !novelText.trimmed().isEmpty();
        if (probeNovel) {
            reportLine(QStringLiteral("novel: %1 paragraph(s)").arg(novelParagraphs.size()));
            for (int i = 0; i < novelParagraphs.size() && i < 20; ++i) {
                const auto& paragraph = novelParagraphs.at(i);
                reportLine(QStringLiteral("  para[%1] (%2,%3 %4x%5) lines=%6 text=%7")
                               .arg(i)
                               .arg(paragraph.rect.x())
                               .arg(paragraph.rect.y())
                               .arg(paragraph.rect.width())
                               .arg(paragraph.rect.height())
                               .arg(paragraph.lineRects.size())
                               .arg(paragraph.text.left(80)));
            }
        }
    }
    const QString probeText = probeUi ? uiTexts.join(QStringLiteral("\n\n"))
                                      : probeNovel ? novelText
                                                   : assembled.text;

    TranslatedDocument doc;
    if (qEnvironmentVariableIsSet("EZ_PROBE_TRANSLATE")) {
        CTranslate2Translator ct2;
        const QString python = CTranslate2Translator::findPython();
        const QString srcLang = sourceLang.isEmpty() ? QStringLiteral("en") : sourceLang;
        const QString modelDir = dir + (srcLang == QLatin1String("zh")
                                             ? QStringLiteral("/ct2-zh-vi")
                                             : QStringLiteral("/ct2-en-vi-int8"));
        if (!python.isEmpty()
            && ct2.start(python, QString(), modelDir, QStringLiteral("int8"),
                         srcLang, QStringLiteral("vi"), &error)) {
            DocumentTranslator translator(nullptr);
            doc = translator.translate(probeText, srcLang, QStringLiteral("vi"),
                                       [&ct2, &srcLang](const QStringList& units) {
                                           return ct2.translate(units, srcLang,
                                                                QStringLiteral("vi"));
                                       });
            reportLine(QStringLiteral("dich that: new = %1").arg(doc.newTranslations));
        } else {
            reportLine(QStringLiteral("ct2 khong san sang, dung text nguon: ") + error);
        }
    }
    if (doc.sourceParagraphs.isEmpty()) {
        DocumentTranslator translator(nullptr);
        doc = translator.translate(probeText,
                                   sourceLang.isEmpty() ? QStringLiteral("en") : sourceLang,
                                   QStringLiteral("vi"), nullptr);
    }

    // ── Layout + plate + render (đúng code app dùng) ─────────────────────────
    QList<OverlayBox> overlayBoxes;
    if (probeUi) {
        int textIndex = 0;
        for (int i = 0; i < uiBoxes.size(); ++i) {
            const OcrTextBox& raw = uiBoxes.at(i);
            const QString rawText = compactCjkSpaces(raw.text.simplified());
            OverlayBox box;
            box.rect = raw.rect;
            box.coverRects.append(raw.rect);
            box.fontPixelSize = qMax(9, int(raw.rect.height() * 0.65));
            box.backdrop = image;
            box.backdropRect = image.rect();
            if (rawText.isEmpty()) {
                box.coverOnly = true;
            } else {
                QString translated = doc.paragraphs.value(textIndex++).trimmed();
                if (translated.isEmpty())
                    translated = rawText;
                box.text = translated;
            }
            overlayBoxes.append(box);
        }
    } else if (probeNovel) {
        overlayBoxes = OverlayLayout::buildNovelBoxes(doc, novelParagraphs, image.rect());
    } else {
        overlayBoxes =
            OverlayLayout::buildParagraphBoxes(doc, lineRects, lineTexts, image.rect(), false);
    }

    QList<QRect> textRects;
    for (const OcrTextBox& box : boxes) {
        if (!box.rect.isEmpty())
            textRects.append(box.rect);
    }
    // In box THÔ của detector (kích thước thật của từng dòng trước khi ghép đoạn) — để soi vì sao
    // plate phủ quá rộng trên UI nhiều khối nhỏ.
    reportLine(QStringLiteral("--- %1 box THO cua detector ---").arg(textRects.size()));
    for (int i = 0; i < textRects.size() && i < 30; ++i) {
        const QRect& rect = textRects.at(i);
        reportLine(QStringLiteral("  raw[%1] (%2,%3 %4x%5)")
                       .arg(i)
                       .arg(rect.x())
                       .arg(rect.y())
                       .arg(rect.width())
                       .arg(rect.height()));
    }
    timer.restart();
    const QImage plate = TextCover::cleanPlate(image, image.rect(), textRects);
    const qint64 plateMs = timer.elapsed();

    const QColor textColor = OverlayLayout::contrastTextColor(image);
    const QColor coverColor = probeNovel ? OverlayLayout::regionPaperColor(image)
                                         : OverlayLayout::regionCoverColor(image);
    for (OverlayBox& box : overlayBoxes) {
        box.textColor = textColor;
        box.coverColor = coverColor;
        box.plate = plate;
        box.plateRect = image.rect();
    }

    TextOverlay overlay;
    overlay.setGlassEnabled(!probeNovel);
    overlay.setAttribute(Qt::WA_DontShowOnScreen, true);
    overlay.showOverTarget(QRect(0, 0, image.width(), image.height()));
    overlay.setBoxes(overlayBoxes, image.size());

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    overlay.render(&out);

    const QString outPath =
        argc > 2 ? QString::fromLocal8Bit(argv[2])
                 : QFileInfo(inPath).absolutePath() + QLatin1Char('/')
                       + QFileInfo(inPath).completeBaseName() + QStringLiteral(".overlay.png");
    out.save(outPath);
    // Lưu luôn PLATE (nền đã xoá chữ) để soi nó xoá tới đâu — cover có lan sang nội dung khác không.
    if (!plate.isNull()) {
        plate.save(QFileInfo(outPath).absolutePath() + QLatin1Char('/')
                   + QFileInfo(inPath).completeBaseName() + QStringLiteral(".plate.png"));
    }
    reportLine(QStringLiteral("plate: %1x%2 (%3 ms) | textColor=%4 coverColor=%5")
                   .arg(plate.width())
                   .arg(plate.height())
                   .arg(plateMs)
                   .arg(textColor.name(), coverColor.name()));
    reportLine(QStringLiteral("luu anh overlay: ") + outPath);

    // ── Kiểm tra cover có phủ quá rộng không ────────────────────────────────
    reportLine(QStringLiteral("--- %1 box ---").arg(overlayBoxes.size()));
    for (int i = 0; i < overlayBoxes.size(); ++i) {
        const OverlayBox& box = overlayBoxes.at(i);
        int coverRight = 0;
        int coverBottom = 0;
        for (const QRect& cover : box.coverRects) {
            coverRight = qMax(coverRight, cover.right());
            coverBottom = qMax(coverBottom, cover.bottom());
        }
        reportLine(QStringLiteral("[%1] box=(%2,%3 %4x%5) coverRects=%6 | cover right=%7 "
                                  "box right=%8 | text=%9")
                       .arg(i)
                       .arg(box.rect.x())
                       .arg(box.rect.y())
                       .arg(box.rect.width())
                       .arg(box.rect.height())
                       .arg(box.coverRects.size())
                       .arg(coverRight)
                       .arg(box.rect.right())
                       .arg(box.text.left(60)));
    }
    report.flush();
    return 0;
}
