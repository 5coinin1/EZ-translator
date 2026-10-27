#pragma once

#include <QRegularExpression>
#include <QString>

namespace EZTranslator {

inline bool isProtectedCode(const QString& text)
{
    const QString line = text.trimmed();
    if (line.isEmpty())
        return false;
    static const QRegularExpression codeStart(
        QStringLiteral(R"(^(//|#include\b|(?:public|private|protected)\s+(?:static\s+)?(?:class|interface|void|int|String)\b|(?:class|def|function|const|let|var)\s+\w+|[{};]|\w+(?:\.\w+)*\s+\w+\s*=\s*\w+\.|\w+(?:\.\w+)*\s*\([^)]*\)\s*[;{]))"));
    return codeStart.match(line).hasMatch();
}

inline bool isPageNumber(const QString& text)
{
    const QString line = text.trimmed();
    if (line.isEmpty() || line.size() > 4)
        return false;
    for (QChar c : line) {
        if (!c.isDigit())
            return false;
    }
    return true;
}

} // namespace EZTranslator
