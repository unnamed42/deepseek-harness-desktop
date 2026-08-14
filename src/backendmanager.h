// backendmanager.h — DeepSeek Harness desktop wrapper
// Owns the lifecycle of the local `dsh web` backend:
//   * reuse an already-running healthy instance, or
//   * spawn the bundled runtime (`node <runtime>/node_modules/.../bin.js web --port 0`)
//     and parse the printed URL line.
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

class BackendManager : public QObject
{
    Q_OBJECT
public:
    explicit BackendManager(QObject *parent = nullptr);
    ~BackendManager() override;

    void start();
    void shutdown(); // graceful stop of a backend we spawned

    QUrl backendUrl() const { return m_url; }
    bool spawnedByUs() const { return m_spawned; }
    QString runtimeDir() const { return m_runtimeDir; }
    QString backendLogPath() const;

    // Overrides (from CLI / environment), empty = auto-detect.
    QString preferredPort; // port of a possibly-already-running instance
    QString directUrl;     // skip detection entirely and load this URL
    QString runtimeOverride; // runtime dir override
    QString nodeOverride;    // node binary override

    static QString defaultPort();

signals:
    void statusChanged(const QString &message);
    void backendReady(const QUrl &url);
    void backendFailed(const QString &detail);

private slots:
    void onProbeFinished(QNetworkReply *reply);
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void pollTick();

private:
    void probeUrl(const QUrl &url, const QString &kind);
    void spawnBackend();
    void startPolling(const QUrl &url, int maxAttempts);
    void fail(const QString &detail);
    void appendLog(const QByteArray &chunk);
    void openLog();

    QProcess *m_process = nullptr;
    QNetworkAccessManager m_nam;
    QTimer m_pollTimer;
    QUrl m_url;
    int m_pollAttempts = 0;
    int m_maxPollAttempts = 0;
    bool m_spawned = false;
    bool m_ready = false;
    bool m_failed = false;
    bool m_shuttingDown = false;
    QString m_runtimeDir;
    QString m_pendingBuffer;
};
