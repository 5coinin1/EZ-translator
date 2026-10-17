#include "ocr/OcrWorker.h"
#include "ocr/OcrEngine.h"

#include <QMutexLocker>

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace EZTranslator
{

    OcrWorker::OcrWorker(QObject *parent) : QThread(parent)
    {
        qRegisterMetaType<EZTranslator::OcrResult>();
    }

    OcrWorker::~OcrWorker() { shutdown(); }

    void OcrWorker::submit(const OcrJob &job)
    {
        QMutexLocker lock(&m_mutex);

        if (m_stopping)
            return;

        m_currentRevision = job.revision;

        // Thay ảnh chờ cũ, không nối thêm vào hàng đợi.
        m_pending = job;
        m_quietTimer.start();
        m_wake.wakeOne();
    }

    void OcrWorker::cancel(quint64 revision)
    {
        QMutexLocker lock(&m_mutex);

        m_currentRevision = revision;
        m_pending.reset();
        m_wake.wakeOne();
    }

    void OcrWorker::shutdown()
    {
        {
            QMutexLocker lock(&m_mutex);

            m_stopping = true;
            m_pending.reset();
            m_wake.wakeAll();
        }

        // Đợi lần xử lý đang chạy kết thúc trước khi hủy luồng.
        wait();
    }

    bool OcrWorker::isCurrent(quint64 revision)
    {
        QMutexLocker lock(&m_mutex);

        return !m_stopping && revision == m_currentRevision;
    }

    void OcrWorker::run()
    {
        // Engine được tạo, sử dụng và hủy trong cùng luồng OCR.
        std::unique_ptr<OcrEngine> engine;
        QString loadedDir;
        bool loadedGpu = false;

        for (;;)
        {
            OcrJob job;

            {
                QMutexLocker lock(&m_mutex);

                for (;;)
                {
                    if (m_stopping)
                        return;

                    if (!m_pending)
                    {
                        m_wake.wait(&m_mutex);
                        continue;
                    }

                    // Chờ ảnh ổn định ngắn trước khi OCR lúc cuộn.
                    const qint64 remaining = 120 - m_quietTimer.elapsed();

                    if (remaining > 0)
                    {
                        m_wake.wait(&m_mutex, static_cast<unsigned long>(remaining));
                        continue;
                    }

                    job = std::move(*m_pending);
                    m_pending.reset();
                    break;
                }
            }

            OcrResult result;
            result.revision = job.revision;
            result.frameSize = job.frameSize;

            QElapsedTimer timer;
            timer.start();

            try
            {
                if (!engine || loadedDir != job.modelsDir || loadedGpu != job.useGpu)
                {
                    engine.reset();

                    auto candidate = std::make_unique<OcrEngine>();

                    OcrOptions options;
                    options.detModelPath = job.modelsDir + "/ch_PP-OCRv4_det_infer.onnx";

                    options.recModelPath = job.modelsDir + "/en_PP-OCRv4_rec_mobile.onnx";

                    options.dictionaryPath = job.modelsDir + "/en_dict.txt";

                    options.useGpu = job.useGpu;

                    QString error;

                    if (!candidate->load(options, &error))
                    {
                        throw std::runtime_error(error.isEmpty() ? "Khong nap duoc model OCR."
                                                                 : error.toStdString());
                    }

                    engine = std::move(candidate);
                    loadedDir = job.modelsDir;
                    loadedGpu = job.useGpu;
                }

                if (!isCurrent(job.revision))
                    continue;

                result.provider = engine->providerName();

                for (const auto &region : job.regions)
                {
                    if (!isCurrent(job.revision))
                        break;

                    if (!region.isValid())
                        continue;

                    OcrRegionResult item;
                    item.pixelRect = region.pixelRect;
                    item.imageSize = region.image.size();
                    item.boxes = engine->recognize(region.image);

                    result.regions.append(std::move(item));
                }
            }
            catch (const std::exception &error)
            {
                result.error = QString::fromUtf8(error.what());
                engine.reset();
            }
            catch (...)
            {
                result.error = "Loi OCR khong xac dinh.";
                engine.reset();
            }

            result.elapsedMs = timer.elapsed();

            if (isCurrent(job.revision))
                emit completed(result);
        }
    }

} // namespace EZTranslator