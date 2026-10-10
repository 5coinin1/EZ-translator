#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class OfflineTranslator : public QObject
{
    Q_OBJECT

public:
    explicit OfflineTranslator(QObject *parent = nullptr);
    ~OfflineTranslator() override;

    bool start(const QString &root, QString *error);
    bool isReady() const { return m_ready; }

    void submit(const QString &text);
    void cancel();

signals:
    void ready();
    void translated(const QString &source, const QString &target);
    void failed(const QString &message);

private:
    void dispatch();
    void readOutput();
    void fail(const QString &message);

    QProcess m_process;
    QTimer m_timer;

    QByteArray m_buffer;
    QByteArray m_stderr;

    bool m_ready{false};
    bool m_busy{false};
    bool m_failed{false};

    quint64 m_serial{0};

    QString m_latestId;
    QString m_activeId;
    QString m_activeText;
    QString m_pendingId;
    QString m_pendingText;
};