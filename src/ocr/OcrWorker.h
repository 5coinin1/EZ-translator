#pragma once

#include "ocr/OcrTypes.h"
#include "region/RegionFrame.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <optional>

namespace EZTranslator
{

    struct OcrJob
    {
        quint64 revision{0};
        QString modelsDir;
        bool useGpu{true};
        QSize frameSize;
        QList<RegionFrame> regions;
    };

    struct OcrRegionResult
    {
        QRect pixelRect;
        QSize imageSize;
        QList<OcrTextBox> boxes;
    };

    struct OcrResult
    {
        quint64 revision{0};
        QSize frameSize;
        QList<OcrRegionResult> regions;
        QString provider;
        QString error;
        qint64 elapsedMs{0};
    };

    class OcrWorker final : public QThread
    {
        Q_OBJECT

    public:
        explicit OcrWorker(QObject *parent = nullptr);
        ~OcrWorker() override;

        void submit(const OcrJob &job);
        void cancel(quint64 revision);
        void shutdown();

    signals:
        void completed(const EZTranslator::OcrResult &result);

    protected:
        void run() override;

    private:
        bool isCurrent(quint64 revision);

        QMutex m_mutex;
        QWaitCondition m_wake;
        std::optional<OcrJob> m_pending;
        QElapsedTimer m_quietTimer;
        quint64 m_currentRevision{0};
        bool m_stopping{false};
    };

} // namespace EZTranslator

Q_DECLARE_METATYPE(EZTranslator::OcrResult)