#pragma once

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include "core/Types.h"
#include "capture/IWindowCapture.h"
#include "region/RegionManager.h"
#include "ocr/OcrScheduler.h"
#include "ocr/OcrRegionMailbox.h"
#include "ocr/OcrTypes.h"
#include "overlay/OverlayLayout.h"
#include "overlay/TextOverlay.h"
#include "scroll/ScrollEstimator.h"
#include "translation/DocumentTranslator.h"
#include "translation/TranslationConfig.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>

#include <atomic>
#include <memory>

class QThread;
class QTimer;
class QShortcut;
class MainWindow;
class RegionEditorWindow;
class RegionSnipperOverlay;
class RegionHighlightOverlay;
class SettingsDialog;
class MiniFloatBar;
class FloatingTranslationWindow;
class TrayIconManager;
class GlobalHotkeys;

namespace EZTranslator {
    class GdiWindowCapture;
    class WgcWindowCapture;
    class RoiPreviewDialog;
    class TranslationWorker;
    class TranslationPopup;
    class OcrWorker;
}

/**
 * AppController – Điều phối luồng và trạng thái giữa các màn hình UI.
 *
 * Đảm bảo các Window không gọi trực tiếp lẫn nhau. Mọi tương tác chuyển màn hình,
 * chuyển trạng thái đều đi qua AppController bằng Qt Signals & Slots.
 */
class AppController : public QObject
{
    Q_OBJECT
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    /** Khởi động ứng dụng, hiển thị MainWindow và khay hệ thống */
    void start();

    /** Nạp sẵn engine ở NỀN ngay khi mở app (model OCR + sidecar dịch en→vi) — bấm Bắt đầu
     *  dịch là có kết quả gần như ngay, không còn chờ cold start >5s cho bản dịch đầu. */
    void preloadEngines();

public slots:
    void onStartTranslation();
    void onStopTranslation();
    void onToggleTranslation();

    void onOpenRegionEditor();
    void onRegionEditorSaved(const QList<EZTranslator::TranslationRegion>& regions);
    void onRegionEditorCancelled();

    void onRegionSnapped(const EZTranslator::NormalizedRect& rect, const QRect& screenRect);
    void onRegionSnapCancelled();
    void onShowRegion();
    void onPreviewRoiRequested();
    void onPreviewClosed();

    /** Thêm vùng mới: khoanh thêm một vùng và GIỮ các vùng đang có. */
    void onRequestAddRegion();
    /** Sửa vùng đang chọn: khoanh lại, thay đúng vùng đó. */
    void onRequestEditRegion(int index);
    /** Xoá vùng đang chọn khỏi danh sách. */
    void onRequestRemoveRegion(int index);
    /** Người dùng đổi vùng đang chọn trong dropdown → đổi vùng thao tác/xem trước. */
    void onRegionSelected(int index);

    void onOpenSettings();
    void onSettingsSaved(const EZTranslator::AppSettings& settings);

    void onShowMainWindow();
    void onQuit();

    /** Goi khi user chon cua so moi tu dropdown */
    void onTargetWindowSelected(quintptr handle, const QString& title, const QString& processName);

    /** Goi khi user doi cap nguon/dich tren MainWindow. */
    void onLanguagesChanged(const QString& source, const QString& target);

    /** Nhan frame tu capture va chuyen len MainWindow preview */
    void onCaptureFrame(const EZTranslator::CapturedFrame& frame);

    /** Nhan loi tu capture va hien thi warning */
    void onCaptureError(EZTranslator::CaptureError error, const QString& detail);

    /** Nhan ket qua dich bat dong bo tu worker thread */
    void onTranslationReady(const QString& regionId, quint64 token,
                            const EZTranslator::TranslatedDocument& document);

    /** OCR worker (thread rieng) bao ket qua. `hoverWorker` = worker riêng của hover (chỉ sinh
     *  khi thực sự dùng "Cửa sổ nổi") — không tính vào trạng thái sẵn sàng của worker vùng. */
    void onOcrLoaded(int generation, bool hoverWorker, bool ok, const QString& provider,
                     const QString& error);
    void onRegionRecognized(const QString& regionId, quint64 requestToken, quint64 imageHash,
                            const QList<EZTranslator::OcrTextBox>& boxes);
    /** Plate dựng sẵn trên luồng OCR về: lưu theo hash nội dung; nếu overlay đang hiện nội dung
     *  này mà chưa có plate thì dán vào và vẽ lại (nâng cấp nền từ "màu đặc" sang "xoá chữ"). */
    void onRegionPlateReady(const QString& regionId, quint64 requestToken, quint64 imageHash,
                            const QImage& plate);
    void onRegionOcrFinished(const QString& regionId, quint64 requestToken);
    void onHoverProbed(const QString& id, const QString& text, const QRect& lineRect,
                       bool clippedByBand);

private:
    void initConnections();

    /**
     * @brief Áp TOÀN BỘ m_settings vào runtime (gọi 1 lần lúc khởi động + mỗi lần Lưu Cài đặt).
     *
     * Gồm: kiểu chữ overlay, loại khỏi capture, fps capture, phím tắt toàn cục,
     * khởi động cùng Windows, và cấu hình worker dịch. Trước đây chỉ có vài dòng
     * rời rạc nên các lựa chọn ở Cài đặt gần như "trang trí".
     */
    void applySettings(bool initial);

    /** Đăng ký lại phím tắt toàn cục theo m_settings; phím bị chiếm → QShortcut dự phòng. */
    void applyHotkeys();

    /** Phím tắt toàn cục vừa bấm (id: "toggle"/"selectRegion"/"showRegion"/"settings"). */
    void dispatchHotkey(const QString& id);
    void queueRegionOcr(const QString& regionId, quint64 requestToken, quint64 imageHash,
                        const QImage& image);
    void dispatchNextRegionOcr();

    /** Chọn backend capture: ưu tiên WGC, fallback GDI. */
    bool startCapture(quintptr windowHandle);

    /** Tùy chọn OCR từ settings (đường dẫn model, GPU/CPU). */
    [[nodiscard]] EZTranslator::OcrOptions ocrOptions(bool hoverWorker = false) const;
    /** Khởi động OCR worker thread và yêu cầu nạp model (bất đồng bộ). */
    void startOcr();

    /** Sinh worker OCR cho VÙNG (hoặc hover) trên thread riêng + nạp model. */
    void spawnOcrWorker(QThread*& thread, EZTranslator::OcrWorker*& worker,
                        const EZTranslator::OcrOptions& options, int generation, bool hoverWorker);

    /** Sinh worker OCR hover theo NHU CẦU (chỉ khi vào chế độ "Cửa sổ nổi"): chế độ Overlay không
     *  dùng hover, mà mỗi worker là một bản engine OCR đầy đủ (~150MB) — không nên nạp sẵn. */
    void ensureHoverOcr();

    /** Dừng OCR worker (nếu có) và xoá trạng thái nhận dạng. */
    void stopOcr();

    /** Nạp lại model OCR sau khi đổi ngôn ngữ nguồn (en ↔ model chung). */
    void reloadOcr();

    /** Tạo engine dịch một lần (lazy). Trả false nếu không có engine. */
    bool ensureTranslator();

    /** Cấu hình worker dịch từ AppSettings (source rỗng = sourceLanguage). */
    [[nodiscard]] EZTranslator::TranslationConfig translationConfig(
        const QString& source = QString()) const;

    /** Vùng client của cửa sổ đích trên màn hình (toạ độ logical của Qt). */
    [[nodiscard]] QRect targetClientRect() const;

    /** Vùng client của cửa sổ đích (toạ độ vật lý/pixel thật). */
    [[nodiscard]] QRect targetClientRectPhysical() const;

    /** Bật/tắt chế độ "dịch khi chỉ chuột" (hover). */
    void startHover();
    void stopHover();
    /** Đồng bộ chế độ hiển thị (Overlay ↔ Cửa sổ nổi) từ UI — hai chế độ ĐỘC LẬP, không chạy
     *  cùng lúc. Đọc lại từ MainWindow thay vì tin cờ cũ (signal có thể bị lỡ). */
    void syncOverlayMode();
    void onHoverTick();
    /** Ẩn/hiện overlay theo tiền cảnh của cửa sổ đích (pipeline vẫn chạy nền). */
    void tickOverlayVisibility();
    /** Ảnh nguồn vừa đổi: ẩn overlay, chờ bản dịch của nội dung mới (KHÔNG hiện bản detect nguồn). */
    void suspendOverlayForChange(qint64 nowMs);
    /** Xử lý một bộ box OCR cho vùng (assembly → box → gửi dịch). buildPlate=false cho đường OCR
     *  tăng dần (bỏ dựng plate cho nhanh; plate dựng ở lượt full/cache hoặc lúc hiện).
     *  approximate=true: box XẤP XỈ (dịch theo ước lượng cuộn) — chỉ hâm nóng cache dịch, KHÔNG
     *  cập nhật trạng thái hiển thị (vị trí xấp xỉ hiện lên rồi bị sửa là lỗi người dùng vừa báo). */
    void handleRegionBoxes(const QString& regionId,
                           const QList<EZTranslator::OcrTextBox>& boxesIn, bool buildPlate,
                           bool approximate = false);
    /** Reconfigure the translation model when OCR reveals a different language family. */
    void maybeUpdateDetectedSource(const QString& text);
    /** Gửi yêu cầu dịch cho vùng (tăng token + nhớ text nguồn của lượt này để neo theo nội dung). */
    void requestRegionTranslation(const QString& regionId, const QString& sourceText);

    /** Tra plate đã dựng sẵn cho vùng theo hash nội dung (khớp trong ngưỡng nhiễu); rỗng nếu chưa có. */
    [[nodiscard]] QImage findRegionPlate(const QString& regionId, quint64 imageHash) const;

    /** Tạm ẩn MainWindow, chụp màn hình và mở lớp khoanh vùng (dùng cho Thêm/Sửa vùng). */
    void startRegionSnipping();

    /** Cập nhật overlay từ box đã tích luỹ theo vùng. */
    void updateOverlay(const QSize& frameSize);

    /** Cập nhật cửa sổ nổi từ source/translation của vùng đang chọn. */
    void refreshFloatingWindow();

    /** Đẩy danh sách vùng hiện tại lên dropdown ở MainWindow (kèm vùng đang chọn). */
    void refreshRegionList();

    /** Xoá toàn bộ trạng thái OCR/dịch/overlay của MỘT vùng (khi sửa hoặc xoá vùng). */
    void forgetRegion(const QString& regionId);

    /** Giai phong cache anh/box cua phien dich (plate full-vung rat ton RAM). */
    void clearVisualCaches();

    MainWindow*               m_mainWindow{nullptr};
    RegionEditorWindow*       m_regionEditor{nullptr};
    RegionSnipperOverlay*     m_snipperOverlay{nullptr};
    RegionHighlightOverlay*   m_highlightOverlay{nullptr};
    SettingsDialog*           m_settingsDialog{nullptr};
    MiniFloatBar*             m_miniFloatBar{nullptr};
    FloatingTranslationWindow* m_floatingWindow{nullptr};
    TrayIconManager*          m_trayManager{nullptr};
    EZTranslator::WgcWindowCapture* m_wgcCapture{nullptr};
    EZTranslator::GdiWindowCapture* m_gdiCapture{nullptr};
    EZTranslator::IWindowCapture*   m_capture{nullptr}; ///< backend đang hoạt động
    EZTranslator::RoiPreviewDialog* m_roiPreviewDialog{nullptr};
    EZTranslator::TextOverlay*      m_textOverlay{nullptr};
    EZTranslator::RegionManager      m_regionManager;
    EZTranslator::OcrScheduler       m_ocrScheduler;
    QThread*                         m_ocrThread{nullptr};      ///< thread OCR cho VÙNG dịch
    EZTranslator::OcrWorker*         m_ocrWorker{nullptr};
    QThread*                         m_hoverOcrThread{nullptr}; ///< thread OCR RIÊNG cho hover
    EZTranslator::OcrWorker*         m_hoverOcrWorker{nullptr};
    QList<QThread*>                  m_retiredOcrThreads;       ///< thread OCR chưa kịp dừng khi reload
    int                              m_ocrGeneration{0};        ///< vô hiệu signal của worker cũ
    QString                          m_ocrProvider;
    bool                             m_hoverOcrReady{false};    ///< worker OCR hover nạp xong (chỉ sinh khi cần)
    QElapsedTimer                    m_clock; ///< đồng hồ đơn điệu cho scheduler
    bool                             m_ocrLoadAttempted{false};
    std::atomic<bool>                m_ocrReady{false}; ///< OCR nạp xong (đọc từ main)
    /// Thông tin hình học của vùng lúc gửi OCR (để dựng overlay khi kết quả về).
    struct RegionFrameInfo { QRect pixelRect; QSize frameSize; };
    QHash<QString, RegionFrameInfo>  m_regionOcrInfo;
    QHash<QString, quint64>          m_regionOcrToken; ///< token OCR mới nhất mỗi vùng
    EZTranslator::OcrRegionMailbox m_regionOcrMailbox;
    /// Nhịp dòng / khe đoạn ĐÃ GHIM theo vùng (chống nhấp nháy cấu trúc đoạn).
    ///
    /// Đo trên frame này rồi dùng lại; chỉ cập nhật khi lệch ĐÁNG KỂ (hysteresis) để box
    /// rung không làm khe đoạn bật/tắt. Nếu không ghim, khe đoạn đổi mỗi frame ⇒ đơn vị
    /// dịch (đoạn) đổi ⇒ cache trượt liên tục và bản dịch nhấp nháy.
    QHash<QString, int>              m_regionLinePitch;
    QHash<QString, double>           m_regionParagraphRatio;
    QThread* m_translateThread{nullptr};
    EZTranslator::TranslationWorker* m_translateWorker{nullptr};
    EZTranslator::TranslationConfig m_translationConfig;
    /// Dòng nguồn (toạ độ frame) theo vùng, để đặt bản dịch khi worker trả kết quả.
    QHash<QString, QList<QRect>> m_regionLines;
    /// Raw OCR boxes khi overlay chay o che do UI (moi label/value mot box).
    QHash<QString, QList<EZTranslator::OcrTextBox>> m_regionUiBoxes;
    /// Paragraph blocks cua che do Novel/reading. Co mat = vung dang o novel mode.
    QHash<QString, QList<EZTranslator::OverlayLayout::NovelParagraph>> m_regionNovelParagraphs;
    /// Giữ quyền thử OCR dải cuộn sau khi một frame tạm thời không còn bố cục novel.
    QSet<QString> m_regionScrollReuseEligible;
    QHash<QString, quint64> m_regionToken; ///< token yêu cầu dịch mới nhất mỗi vùng
    quint64 m_tokenCounter{0};
    QSize m_lastFrameSize;
    QString m_detectedSource; ///< ngôn ngữ nguồn tự nhận diện (khi settings = "auto")

    // ── Chế độ "dịch khi chỉ chuột" (hover) ──────────────────────────────────
    EZTranslator::TranslationPopup* m_hoverPopup{nullptr};
    QTimer* m_hoverTimer{nullptr};
    /// Timer 250ms giữ overlay khớp tiền cảnh: ẩn khi cửa sổ đích bị che, hiện lại ngay khi quay
    /// về (kể cả khi không có frame mới nào để chạy updateOverlay).
    QTimer* m_visibilityTimer{nullptr};
    /// Timer 50ms xử lý lại FRAME CUỐI: cửa sổ tĩnh không phát frame nên settle/cooldown phải
    /// được kiểm tra theo nhịp cố định, nếu không OCR đợi tới lần vẽ lại kế tiếp (trễ 1-2s).
    QTimer* m_catchUpTimer{nullptr};
    EZTranslator::CapturedFrame m_lastCapture;
    quintptr m_captureHandle{0};
    bool     m_gotFirstFrame{false}; ///< đã nhận frame nào từ backend capture chưa
    quint64  m_captureFramesSeen{0};
    qint64   m_captureDiagAtMs{-1000000};
    QImage m_hoverFrame;             ///< frame capture mới nhất (để dò dòng dưới chuột)
    QSize  m_hoverFrameSize;
    bool   m_hoverFrameReady{false};
    int    m_hoverFrameSettle{0};
    int    m_hoverEmptyStreak{0};
    qint64 m_hoverLastValidMs{0};
    QRect  m_hoverClientPhysical;    ///< vùng client (pixel thật) để map con trỏ
    QPoint m_hoverLastCursor;        ///< để phát hiện con trỏ đứng yên
    qint64 m_hoverStillSince{0};
    qint64 m_hoverLastProbeMs{-1000000}; ///< chặn dò lại liên tục khi con trỏ đứng yên
    qint64 m_hoverProbeMs{0};            ///< thời điểm bắt đầu probe (đo độ trễ dịch)
    qint64 m_hoverLastSendMs{-1000000};  ///< lần gửi probe gần nhất (throttle dò trước)
    qint64 m_hoverDiagMs{-1000000};      ///< lần ghi log chẩn đoán gần nhất (2s/lần)
    QPoint m_hoverSentCursor;            ///< vị trí con trỏ của lần gửi probe gần nhất
    bool   m_hoverLineValid{false};
    QRect  m_hoverLineClient;        ///< dòng đang trỏ (toạ độ client)
    int    m_hoverLineHeight{0};     ///< chiều cao dòng (frame px)
    QString m_hoverSource;
    QString m_hoverTranslation;
    quint64 m_hoverToken{0};          ///< token của PROBE (dò dòng)
    quint64 m_hoverTranslationToken{0}; ///< token của lượt DỊCH hover (tách khỏi probe!)
    QPoint  m_hoverLineCursor;       ///< con trỏ tại dòng đang hiển thị/đợi dịch (đặt popup)
    QPoint  m_hoverProbedCursor;     ///< vị trí con trỏ đã dò xong (chặn dò lại)
    bool    m_hoverPending{false};   ///< đã dò được dòng, đang chờ bản dịch để hiện 1 lần
    QHash<QString, QString> m_hoverCache;

    /// Hình học + mục đích của từng probe đang chờ. Kết quả trả về theo token nên
    /// probe "dò trước" và probe cuối không tranh nhau state dùng chung.
    struct HoverProbeGeom
    {
        QPoint bandOrigin;
        double scaleX{1.0};
        double scaleY{1.0};
        QPoint cursorScreen;
        bool   prefetch{false};
        int    stage{0};             ///< bước mở rộng dải (0 = dải thường)
        bool   canGrow{false};       ///< dải còn chỗ để mở rộng (chưa chạm biên frame cả 2 trục)
    };
    QHash<quint64, HoverProbeGeom> m_hoverProbes;
    quint64 m_hoverLastFinalToken{0}; ///< token của probe "cuối" (không phải dò trước)
    bool m_hoverProbeRunning{false}; ///< tối đa một probe đang chạy trong worker hover
    struct PendingHoverProbe
    {
        QPoint screenPoint;
        bool prefetch{false};
        int stage{0};
        bool valid{false};
    };
    PendingHoverProbe m_pendingHoverProbe;

    /// Kết quả dò trước (đo trong lúc con trỏ còn đang di chuyển) để dùng lại khi dừng.
    struct HoverPrefetch
    {
        QString text;
        QRect   lineClient;
        qint64  atMs{-1000000};
    };
    HoverPrefetch m_hoverPrefetch;

    /** Dựng dải quanh con trỏ và gửi yêu cầu dò sang worker OCR hover.
     *
     *  @param stage bước MỞ RỘNG: 0 = dải thường (kích thước cố định, nhanh); các bước
     *               sau rộng dần, chỉ dùng khi cụm chữ con trỏ đang ở vẫn chạm mép dải
     *               (câu dài hơn dải). Dừng ngay khi cụm kết thúc ở KHE — biên tự nhiên
     *               do khoảng đệm tới phần tử khác quyết định (kiểu meikipop).
     */
    void sendHoverProbe(const QPoint& screenPoint, bool prefetch, int stage = 0);
    void flushPendingHoverProbe();

    /** Hiện bản dịch cho một dòng (hiện nguồn + bản dịch cùng lúc, hoặc đợi worker). */
    void showHoverLine(const QString& text, const QRect& lineClient,
                       const QPoint& cursorScreen);
    /// Box hiển thị theo từng vùng (toạ độ frame), giữ lại để overlay luôn đầy đủ
    /// kể cả khi vùng đó không đổi / bản dịch còn đang chờ ở frame hiện tại.
    QHash<QString, QList<EZTranslator::OverlayBox>> m_regionBoxes;
    /// Văn bản nguồn OCR cuối của từng vùng; nếu không đổi thì giữ overlay dịch cũ
    /// trong lúc lượt dịch mới đang chạy (tránh nháy ngược về chữ OCR mỗi frame).
    QHash<QString, QString> m_regionSourceText;
    /// Bản dịch đã ghép của từng vùng, dùng cho cửa sổ nổi.
    QHash<QString, QString> m_regionTranslatedText;
    /// Bộ nhớ dịch theo đoạn (chuẩn hoá nội dung): cuộn lên/xuống gặp lại
    /// đoạn cũ thì dùng đúng bản dịch cũ, không gọi engine lại.
    QHash<QString, QString> m_paragraphTranslations;

    /// Ảnh ROI mới nhất đã gửi OCR cho từng vùng (để dựng smart-cover plate khớp ĐÚNG nội dung
    /// mà các box được OCR ra — plate cũ dán lên nội dung mới là "chữ thật lẫn chữ dịch").
    QHash<QString, QImage> m_regionImages;
    /// Text của từng dòng nguồn (cùng thứ tự với m_regionLines) — ParagraphAnchor khớp đoạn theo
    /// nội dung cần chúng.
    QHash<QString, QStringList> m_regionLineTexts;
    /// Thời điểm gửi OCR / gửi dịch của từng vùng — để log độ trễ thật (tick→OCR→dịch→hiện).
    QHash<QString, qint64> m_regionOcrAtMs;
    QHash<QString, qint64> m_regionTranslateAtMs;
    /// CACHE BOX THEO NỘI DUNG: mỗi vùng giữ box của các nội dung (hash) đã từng OCR — gặp lại là
    /// dùng ngay, không OCR lại (cuộn qua lại vùng đã xem tức thì). Giới hạn 8 nội dung/vùng.
    QHash<QString, QHash<quint64, QList<EZTranslator::OcrTextBox>>> m_ocrBoxCache;
    /// Hash của ảnh đã gửi OCR cho từng vùng — khoá để lưu box vào cache khi worker trả về.
    QHash<QString, quint64> m_regionOcrHash;
    /// Thời điểm nội dung vùng đổi lần cuối — cửa sổ "đang chuyển động" 320ms chặn OCR full giữa
    /// lúc cuộn (kiến trúc translator).
    QHash<QString, qint64> m_regionLastChangeMs;
    /// Hash nội dung (dHash 64-bit) của bản ĐÃ PHỤC VỤ mỗi vùng (OCR full hoặc cache box). Khung
    /// sau chỉ nhiễu capture trong ngưỡng "không đổi" (maxDistance) thì BỎ QUA cả vùng: không OCR,
    /// không gửi dịch lại, không đặt lại overlay — trước đây so hash TUYỆT ĐỐI nên nhiễu 1-4 bit
    /// làm miss mỗi khung ⇒ OCR full + dịch + đặt lại ~17 lần/giây (log thật 20:03-20:06).
    QHash<QString, quint64> m_regionServedHash;
    /// Profile 1D của ảnh vùng lúc PHỤC VỤ: hash dHash 8×8 quá thô — cuộn 1 dòng có thể chỉ lệch
    /// ≤4 bit nên bị coi là "không đổi" rồi bỏ qua ⇒ text cũ nằm sai vị trí mãi ("thi thoảng sai
    /// vị trí"). Profile so được ĐỘ DỊCH thật (1-2px cũng thấy) mà rất rẻ.
    QHash<QString, GrayProfile> m_regionServedProfile;
    QHash<QString, GrayProfile> m_regionObservedProfile;
    /// Thời điểm trạng thái CHÍNH XÁC (dòng/nguồn) của vùng được ghi (chỉ đường full OCR / cache
    /// nội dung). Kết quả dịch chỉ được áp khi không có lần đổi nội dung nào mới hơn mốc này.
    QHash<QString, qint64> m_regionStateAtMs;
    /// Text nguồn của từng lượt dịch đang bay (region → token → text): kết quả chỉ được áp khi text
    /// của CHÍNH lượt đó còn khớp text nguồn hiện tại (neo theo nội dung, không bỏ theo token).
    QHash<QString, QHash<quint64, QString>> m_regionRequestSource;
    /// Overlay đang treo vì ảnh nguồn vừa đổi (chờ bản dịch của nội dung mới). Khi treo thì không
    /// vẽ gì — không hiện bản detect nguồn như trước.
    bool m_overlaySuspended = false;
    qint64 m_overlaySuspendAtMs = 0;
    /// Box bản NGUỒN đã detect của từng vùng — chỉ dùng làm fallback 2s nếu bản dịch không về.
    QHash<QString, QList<EZTranslator::OverlayBox>> m_pendingSourceBoxes;
    /// Smart-cover plate (nền đã xoá chữ gốc, giữ UI bên dưới) của từng vùng + bộ dòng nó ứng với.
    /// Plate (nền đã xoá chữ) dựng SẴN trên LUỒNG OCR, key theo hash nội dung ảnh (mỗi vùng giữ
    /// vài nội dung mới nhất). Luồng UI chỉ TRA và dán — không dựng (xem OcrWorker).
    QHash<QString, QHash<quint64, QImage>> m_regionPlates;

    EZTranslator::TranslationState m_state{EZTranslator::TranslationState::Idle};
    EZTranslator::AppSettings m_settings;
    GlobalHotkeys*            m_hotkeys{nullptr};
    QList<QShortcut*>         m_hotkeyFallbacks; ///< dự phòng khi phím bị app khác chiếm
    QList<EZTranslator::TranslationRegion> m_regions;
    QRect m_currentScreenRect;
    /// Chế độ hiển thị ở Section 3: false = cửa sổ nổi (popup hover), true = overlay đè lên game.
    /// Chỉ MỘT trong hai được hiển thị tại một thời điểm.
    bool m_overlayMode{false};
    /// Cửa sổ nổi đang hiển thị (khác overlay và khác hover).
    bool m_floatingMode{false};
    /// Toạ độ màn hình (pixel thật) của từng vùng — cần cho "Xem vùng" theo vùng đang chọn.
    QHash<QString, QRect> m_regionScreenRects;
    int  m_editRegionIndex{-1};    ///< >= 0: lần khoanh tới sẽ THAY vùng này; -1: thêm vùng mới
    int  m_currentRegionIndex{-1}; ///< vùng đang chọn trong dropdown
    int  m_nextRegionNumber{1};    ///< số thứ tự cho id/tên vùng mới
    bool  m_captureStartedForPreview{false};
};
