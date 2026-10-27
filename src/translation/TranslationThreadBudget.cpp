#include "translation/TranslationThreadBudget.h"

#include <QThread>

namespace EZTranslator {

int chooseTranslationThreads()
{
    const int fallback = qMax(1, QThread::idealThreadCount() / 4);
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue("EZ_TRANSLATE_THREADS", &ok);
    return ok && requested > 0 ? qBound(1, requested, 16) : fallback;
}

} // namespace EZTranslator
