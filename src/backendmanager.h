// backendmanager.h — DeepSeek Harness desktop wrapper
// Owns the lifecycle of the local `dsh web` backend:
//   1. reuse an already-running healthy instance (probe + boot-manifest check), or
//   2. use the systemd user service: start an equivalent existing unit, or
//      enable --now the shipped dsh-web.service when nothing equivalent
//      exists yet (the "don't add if an equivalent service exists" guard), or
//   3. fall back to spawning `dsh web --port 0` from PATH directly.
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
    void shutdown(); // graceful stop of a backend we spawned (services are left running)

    QUrl backendUrl() const { return m_url; }
    bool spawnedByUs() const { return m_spawned; }
    QString backendLogPath() const;

    // Overrides (from CLI / environment), empty = auto-detect.
    QString preferredPort; // port of a possibly-already-running instance
    QString directUrl;     // skip detection entirely and load this URL
    bool noService = false; // never touch systemd user services

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
    struct ServiceInfo
    {
        QString unit;              // unit name ("" = none found)
        bool enableInsteadOfStart = false; // unit exists but is not enabled yet
    };

    void probeUrl(const QUrl &url, const QString &kind);
    void spawnBackend();
    void tryServiceOrSpawn(int preferredPort);
    ServiceInfo detectEquivalentService();
    QUrl readServiceUrl(const QString &unit);
    void startServiceUrlPolling(const QString &unit);
    void pollServiceUrl();
    QString sanitizeUrl(const QUrl &url) const;
    bool unitFileExists(const QString &name);
    bool startServiceUnit(const QString &unit);
    bool enableServiceUnit(const QString &unit);
    void startPolling(const QUrl &url, int maxAttempts);
    void fail(const QString &detail);
    void appendLog(const QByteArray &chunk);

    QProcess *m_process = nullptr;
    QNetworkAccessManager m_nam;
    QTimer m_pollTimer;
    QTimer m_serviceUrlTimer;
    QString m_serviceUnit;
    int m_serviceUrlAttempts = 0;
    int m_serviceUrlMaxAttempts = 0;
    QUrl m_url;
    int m_pollAttempts = 0;
    int m_maxPollAttempts = 0;
    bool m_pollFallbackToSpawn = false;
    bool m_spawned = false;
    bool m_ready = false;
    bool m_failed = false;
    bool m_shuttingDown = false;
    QString m_pendingBuffer;
};
