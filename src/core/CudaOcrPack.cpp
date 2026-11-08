#include "core/CudaOcrPack.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
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
    // "auto" phải thực sự ưu tiên CUDA nếu gói đã cài; trước đây chỉ "cuda"
    // mới kích hoạt profile, nên auto luôn chạy base DirectML.
    const auto wantsCuda = [](const QString& device) {
        return device.compare(QStringLiteral("cuda"), Qt::CaseInsensitive) == 0
               || device.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    };
    return wantsCuda(settings.ocrDetDevice) || wantsCuda(settings.ocrRecDevice);
}

void refreshProfileAppFiles()
{
    const QString base = QCoreApplication::applicationDirPath();
    const QString dest = profileDir();
    QDir().mkpath(dest);
    for (const char* name : {"EZTranslator.exe", "wgc_capture_helper.exe", "ct2_server.py"}) {
        const QString source = QDir(base).filePath(QString::fromLatin1(name));
        const QString target = QDir(dest).filePath(QString::fromLatin1(name));
        const QFileInfo srcInfo(source);
        const QFileInfo dstInfo(target);
        if (!srcInfo.exists())
            continue;
        if (dstInfo.exists() && dstInfo.size() == srcInfo.size()
            && dstInfo.lastModified() >= srcInfo.lastModified()) {
            continue;
        }
        if (dstInfo.exists())
            QFile::remove(target);
        if (!QFile::copy(source, target))
            qWarning() << "[CUDA] khong refresh duoc" << name << "vao profile";
    }
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
