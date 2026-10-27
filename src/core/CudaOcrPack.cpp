#include "core/CudaOcrPack.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace EZTranslator::CudaOcrPack {

QString profileDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("cuda"));
}

QString profileExe()
{
    return QDir(profileDir()).filePath(QStringLiteral("EZTranslator.exe"));
}

QString markerPath()
{
    return QDir(profileDir()).filePath(QStringLiteral("cuda_ocr_ready.txt"));
}

QString envPath()
{
    return QDir(profileDir()).filePath(QStringLiteral("cuda_env.txt"));
}

QString installScriptPath()
{
    const QString beside =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("install_cuda_ocr.ps1"));
    if (QFileInfo::exists(beside))
        return beside;

    // Dev build: script nằm trong source tree, app có thể chưa copy.
    const QString source = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("../../scripts/install_cuda_ocr.ps1"));
    return QFileInfo::exists(source) ? QFileInfo(source).absoluteFilePath() : beside;
}

bool isInstalled()
{
    return QFileInfo::exists(profileExe()) && QFileInfo::exists(markerPath());
}

bool requested(const AppSettings& settings)
{
    return settings.ocrDetDevice.compare(QStringLiteral("cuda"), Qt::CaseInsensitive) == 0
           || settings.ocrRecDevice.compare(QStringLiteral("cuda"), Qt::CaseInsensitive) == 0;
}

bool isProfileProcess()
{
    if (qEnvironmentVariableIsSet("EZ_CUDA_PROFILE"))
        return true;
    const QString appDir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    const QString profile = QFileInfo(profileDir()).canonicalFilePath();
    return !appDir.isEmpty() && !profile.isEmpty() && appDir == profile;
}

} // namespace EZTranslator::CudaOcrPack
