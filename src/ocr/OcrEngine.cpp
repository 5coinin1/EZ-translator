#include "ocr/OcrEngine.h"

#include "ocr/OcrDetector.h"
#include "ocr/OcrRecognizer.h"
#include "ocr/OcrRuntime.h"

namespace EZTranslator {

struct OcrEngine::Impl
{
    OcrRuntime runtime;
    OcrDetector detector;
    OcrRecognizer recognizer;
    bool loaded{false};
    int lastBoxes{0};
};

OcrEngine::OcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

OcrEngine::~OcrEngine() = default;

bool OcrEngine::load(const OcrOptions& options, QString* error)
{
    if (!m_impl)
        return false;

    m_impl->runtime.configure(options.useGpu, error);

    if (!m_impl->detector.load(m_impl->runtime, options, error))
        return false;
    if (!m_impl->recognizer.load(m_impl->runtime, options, error))
        return false;

    m_impl->loaded = true;
    return true;
}

bool OcrEngine::isLoaded() const
{
    return m_impl && m_impl->loaded;
}

QString OcrEngine::providerName() const
{
    return m_impl ? m_impl->runtime.providerName() : QString();
}

QList<OcrTextBox> OcrEngine::recognize(const QImage& image)
{
    if (!isLoaded() || image.isNull())
        return {};

    const QList<OcrTextBox> boxes = m_impl->detector.detect(image);
    m_impl->lastBoxes = boxes.size();
    return m_impl->recognizer.recognize(image, boxes);
}

OcrTimings OcrEngine::lastTimings() const
{
    OcrTimings timings = m_impl ? m_impl->recognizer.lastTimings() : OcrTimings();
    if (m_impl) {
        timings.detMs = m_impl->detector.lastMs();
        timings.boxes = m_impl->lastBoxes;
    }
    return timings;
}

OcrDetector& OcrEngine::detector()
{
    return m_impl->detector;
}

OcrRecognizer& OcrEngine::recognizer()
{
    return m_impl->recognizer;
}

} // namespace EZTranslator
