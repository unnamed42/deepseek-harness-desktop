// backendmanager.cpp — DeepSeek Harness desktop wrapper
#include "backendmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr int kProbeTimeoutMs = 2500;    // per-request transfer timeout
constexpr int kPollIntervalMs = 300;     // readiness poll interval
constexpr int kUrlLineTimeoutMs = 20000; // wait for the printed URL line
constexpr int kReadyPollAttempts = 300;  // ~90 s while a spawned backend boots
constexpr int kServicePollAttempts = 200; // ~60 s for a systemd-managed backend
constexpr int kDirectPollAttempts = 1200; // ~6 min for a user-managed URL
constexpr int kSystemctlTimeoutMs = 30000;
constexpr int kServiceUrlPollIntervalMs = 500; // journal/journalctl poll interval
constexpr int kServiceUrlPollAttempts = 120;  // ~60 s waiting for the URL line

const char *kProbeExisting = "probe-existing";
const char *kReadyPoll = "ready-poll";
const char *kServiceUnit = "dsh-web.service";

struct SysResult
{
    int exitCode = -1;
    QString output;
};

SysResult runCommand(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(3000))
        return {};
    if (!process.waitForFinished(kSystemctlTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    return {process.exitCode(), QString::fromUtf8(process.readAllStandardOutput())};
}

SysResult runSystemctl(const QStringList &arguments)
{
    return runCommand(QStringLiteral("systemctl"),
                      QStringList{QStringLiteral("--user")} + arguments);
}

// Parse the first "dsh web: <url>" line in the given text. `dsh web` prints
// the reachable URL (now carrying an auth token, e.g.
// "dsh web: http://127.0.0.1:3080/?token=...") to its logs on startup; that is
// the ONLY place the token is exposed, so it must come from the logs rather
// than from the unit file. The caller passes reverse-chronological text so the
// first match is the newest boot's URL.
QUrl parseHarnessUrl(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("dsh web:\\s*(https?://\\S+)"));
    const QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch())
        return QUrl(match.captured(1));
    return {};
}

QString dshExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("dsh"));
}

QString userUnitDir()
{
    const QString configHome = qEnvironmentVariable(
        "XDG_CONFIG_HOME", QDir::home().filePath(QStringLiteral(".config")));
    return configHome + QStringLiteral("/systemd/user");
}
} // namespace

BackendManager::BackendManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_nam, &QNetworkAccessManager::finished, this, &BackendManager::onProbeFinished);
}

BackendManager::~BackendManager()
{
    shutdown();
}

QString BackendManager::defaultPort()
{
    const QString env = qEnvironmentVariable("DSH_DESKTOP_PORT");
    return env.isEmpty() ? QStringLiteral("3080") : env;
}

QString BackendManager::backendLogPath() const
{
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dirPath.isEmpty())
        dirPath = QDir::home().filePath(QStringLiteral(".local/share/deepseek/deepseek-harness-desktop"));
    return dirPath + QStringLiteral("/backend.log");
}

void BackendManager::appendLog(const QByteArray &chunk)
{
    QFile f(backendLogPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Append))
        f.write(chunk);
}

void BackendManager::start()
{
    if (!qEnvironmentVariableIsEmpty("DSH_DESKTOP_DEBUG")) {
        connect(this, &BackendManager::statusChanged, this, [this](const QString &message) {
            appendLog(QByteArray("[status] ") + message.toUtf8() + QByteArray("\n"));
        });
    }
    appendLog(QByteArray("\n==== ")
              + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
              + QByteArray(" launch ====\n"));

    if (!directUrl.isEmpty()) {
        m_url = QUrl::fromUserInput(directUrl);
        emit statusChanged(tr("正在连接 %1 …").arg(sanitizeUrl(m_url)));
        startPolling(m_url, kDirectPollAttempts);
        return;
    }

    QString port = preferredPort;
    if (port.isEmpty())
        port = defaultPort();
    if (port == QLatin1String("0")) {
        spawnBackend();
        return;
    }

    emit statusChanged(tr("正在检测本地 Harness 服务 (127.0.0.1:%1) …").arg(port));
    probeUrl(QUrl(QStringLiteral("http://127.0.0.1:") + port + QLatin1Char('/')),
             kProbeExisting);
}

void BackendManager::probeUrl(const QUrl &url, const QString &kind)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::User, kind);
    request.setTransferTimeout(kProbeTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    m_nam.get(request);
}

void BackendManager::onProbeFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_shuttingDown || m_ready || m_failed)
        return;

    const QString kind = reply->request().attribute(QNetworkRequest::User).toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (kind == QLatin1String(kProbeExisting)) {
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            const QByteArray body = reply->readAll();
            if (body.contains("__DSH_BOOT__") && body.contains("DeepSeek Harness")) {
                // A healthy harness instance is already listening there: reuse it.
                m_url = reply->url();
                m_spawned = false;
                m_ready = true;
                emit statusChanged(tr("已连接到本地 Harness 服务: %1").arg(sanitizeUrl(m_url)));
                emit backendReady(m_url);
                return;
            }
            // Some unrelated service owns that port; never hijack it.
        }
        if (noService)
            spawnBackend();
        else
            tryServiceOrSpawn(reply->url().port());
    } else if (kind == QLatin1String(kReadyPoll)) {
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            m_ready = true;
            m_pollTimer.stop();
            emit statusChanged(tr("Harness 服务已就绪: %1").arg(sanitizeUrl(m_url)));
            emit backendReady(m_url);
        }
        // otherwise the poll timer keeps going
    }
}

// --- systemd user service management -------------------------------------

bool BackendManager::unitFileExists(const QString &name)
{
    const QStringList candidates = {
        userUnitDir() + QLatin1Char('/') + name,
        QStringLiteral("/etc/systemd/user/") + name,
        QStringLiteral("/usr/lib/systemd/user/") + name,
        QStringLiteral("/usr/local/lib/systemd/user/") + name,
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return true;
    }
    return false;
}

BackendManager::ServiceInfo BackendManager::detectEquivalentService()
{
    ServiceInfo none;

    // 1. The canonical unit, in any state (installed, enabled, masked, ...).
    const SysResult list =
        runSystemctl({QStringLiteral("list-unit-files"), QStringLiteral("--type=service"),
                      QStringLiteral("--no-legend"), QStringLiteral("--no-pager")});
    if (list.exitCode == 0) {
        const QStringList lines = list.output.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QStringList parts = QString(line).simplified().split(QLatin1Char(' '),
                                                                      Qt::SkipEmptyParts);
            if (parts.size() < 2)
                continue;
            const QString name = parts.at(0);
            const QString state = parts.at(1);
            if (name == QLatin1String(kServiceUnit)) {
                // Already registered by us (or the user's own copy):
                // just start it. A disabled copy still needs enabling —
                // that is the "add the service" step.
                ServiceInfo info;
                info.unit = name;
                info.enableInsteadOfStart =
                    state.startsWith(QStringLiteral("disabled"))
                    || state.startsWith(QStringLiteral("masked"))
                    || state == QLatin1String("bad");
                return info;
            }
        }
    }

    // 2. Any other user unit that provides the same effect (runs `dsh web`).
    //    "如果有效果相同的服务就不添加该服务": use it, don't register ours.
    const QStringList dirs = {
        userUnitDir(),
        QStringLiteral("/etc/systemd/user"),
        QStringLiteral("/usr/lib/systemd/user"),
        QStringLiteral("/usr/local/lib/systemd/user"),
    };
    for (const QString &dirPath : dirs) {
        const QDir dir(dirPath);
        const QFileInfoList entries =
            dir.entryInfoList({QStringLiteral("*.service")}, QDir::Files, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (info.fileName() == QLatin1String(kServiceUnit))
                continue; // already handled above
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString content = QString::fromUtf8(f.readAll());
            if (content.contains(QStringLiteral("dsh web"))) {
                ServiceInfo found;
                found.unit = info.fileName();
                return found;
            }
        }
    }
    return none;
}

QString BackendManager::sanitizeUrl(const QUrl &url) const
{
    if (!url.hasQuery())
        return url.toString();
    // Don't print the auth token in status messages / logs.
    QString masked = url.toString(QUrl::RemoveQuery);
    masked += QLatin1String("?token=***");
    return masked;
}

QUrl BackendManager::readServiceUrl(const QString &unit)
{
    // The auth token (part of the full reachable URL) is only printed by
    // `dsh web` on startup and is NOWHERE in the unit file, so resolve the
    // URL from the service's own logs instead of parsing the ExecStart.
    // 1. journalctl -r: new entries FIRST, so the first `dsh web:` URL match
    //    is the newest boot's (with the current auth token).
    const SysResult journal = runCommand(
        QStringLiteral("journalctl"),
        {QStringLiteral("--user"), QStringLiteral("-u"), unit,
         QStringLiteral("-r"), QStringLiteral("--no-pager"),
         QStringLiteral("-o"), QStringLiteral("cat")});
    if (journal.exitCode == 0) {
        const QUrl url = parseHarnessUrl(journal.output);
        if (!url.isEmpty())
            return url;
    }
    // 2. systemctl --user status: recent log lines carried alongside status.
    const SysResult status = runSystemctl({QStringLiteral("status"), unit});
    if (status.exitCode == 0) {
        const QUrl url = parseHarnessUrl(status.output);
        if (!url.isEmpty())
            return url;
    }
    return {};
}

void BackendManager::startServiceUrlPolling(const QString &unit)
{
    m_serviceUnit = unit;
    m_serviceUrlAttempts = 0;
    m_serviceUrlMaxAttempts = kServiceUrlPollAttempts;
    connect(&m_serviceUrlTimer, &QTimer::timeout, this, &BackendManager::pollServiceUrl);
    m_serviceUrlTimer.start(kServiceUrlPollIntervalMs);
    pollServiceUrl();
}

void BackendManager::pollServiceUrl()
{
    if (m_shuttingDown || m_ready || m_failed)
        return;
    ++m_serviceUrlAttempts;
    const QUrl url = readServiceUrl(m_serviceUnit);
    if (!url.isEmpty()) {
        m_serviceUrlTimer.stop();
        emit statusChanged(tr("服务 %1 监听于 %2，等待就绪…")
                               .arg(m_serviceUnit, sanitizeUrl(url)));
        m_pollFallbackToSpawn = true;
        startPolling(url, kServicePollAttempts);
        return;
    }
    if (m_serviceUrlAttempts > m_serviceUrlMaxAttempts) {
        m_serviceUrlTimer.stop();
        emit statusChanged(
            tr("从服务 %1 的日志中未解析出监听 URL，回退到直接运行 dsh web…")
                .arg(m_serviceUnit));
        spawnBackend();
        return;
    }
    if (m_serviceUrlAttempts % 10 == 1) {
        emit statusChanged(tr("等待服务 %1 输出监听 URL (%2s)…")
                               .arg(m_serviceUnit)
                               .arg((m_serviceUrlAttempts * kServiceUrlPollIntervalMs) / 1000));
    }
}

bool BackendManager::startServiceUnit(const QString &unit)
{
    const SysResult result = runSystemctl({QStringLiteral("start"), unit});
    return result.exitCode == 0;
}

bool BackendManager::enableServiceUnit(const QString &unit)
{
    const SysResult result =
        runSystemctl({QStringLiteral("enable"), QStringLiteral("--now"), unit});
    return result.exitCode == 0;
}

void BackendManager::tryServiceOrSpawn(int preferredPort)
{
    Q_UNUSED(preferredPort);
    // Guard: never touch systemd when the user opted out.
    if (noService) {
        spawnBackend();
        return;
    }

    // "如果有效果相同的服务就不添加该服务":
    // an already-registered equivalent service is only started, never re-added.
    const ServiceInfo info = detectEquivalentService();
    if (!info.unit.isEmpty()) {
        bool ok = false;
        if (info.enableInsteadOfStart) {
            emit statusChanged(
                tr("未发现已启用的等效服务，正在启用 %1 …").arg(info.unit));
            ok = enableServiceUnit(info.unit);
            if (ok)
                emit statusChanged(tr("%1 已启用，等待就绪…").arg(info.unit));
        } else {
            emit statusChanged(
                tr("发现已注册的等效服务 %1，正在启动…").arg(info.unit));
            ok = startServiceUnit(info.unit);
            if (ok)
                emit statusChanged(tr("服务 %1 已启动，等待就绪…").arg(info.unit));
        }
        if (!ok) {
            emit statusChanged(tr("服务 %1 启动/启用失败，回退到直接运行 dsh web…")
                                   .arg(info.unit));
            spawnBackend();
            return;
        }
        // Resolve the reachable URL (port + auth token) from the service logs.
        startServiceUrlPolling(info.unit);
        return;
    }

    // Nothing equivalent found: register ours, if the unit file was shipped.
    if (unitFileExists(QLatin1String(kServiceUnit))) {
        emit statusChanged(
            tr("未发现等效服务，正在注册并启动 %1 …").arg(QLatin1String(kServiceUnit)));
        if (enableServiceUnit(QLatin1String(kServiceUnit))) {
            emit statusChanged(tr("%1 已启用，等待就绪…").arg(QLatin1String(kServiceUnit)));
            // Resolve the reachable URL (port + auth token) from the service logs.
            startServiceUrlPolling(QLatin1String(kServiceUnit));
            return;
        }
        emit statusChanged(tr("%1 注册/启动失败，回退到直接运行 dsh web…")
                               .arg(QLatin1String(kServiceUnit)));
    }

    spawnBackend();
}

// --- direct process spawn (fallback) -------------------------------------

void BackendManager::spawnBackend()
{
    if (m_shuttingDown || m_failed)
        return;

    const QString dsh = dshExecutable();
    if (dsh.isEmpty()) {
        fail(tr("未找到 dsh 命令。请先安装 deepseek-harness-git（提供 /usr/bin/dsh），"
                "或将其加入 PATH。"));
        return;
    }

    m_spawned = true;
    m_pendingBuffer.clear();
    emit statusChanged(tr("正在启动 DeepSeek Harness 后端 (dsh web)…"));

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &BackendManager::onProcessReadyRead);
    connect(m_process, &QProcess::finished, this, &BackendManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &BackendManager::onProcessErrorOccurred);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(QDir::homePath());
    m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process->setProgram(dsh);
    m_process->setArguments({QStringLiteral("web"), QStringLiteral("--host"),
                             QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                             QStringLiteral("0")});
    m_process->start();

    QTimer::singleShot(kUrlLineTimeoutMs, this, [this] {
        if (!m_ready && !m_failed && !m_shuttingDown && m_process
            && m_process->state() != QProcess::NotRunning) {
            fail(tr("后端已启动，但 %1 秒内未解析出监听端口。日志: %2")
                     .arg(kUrlLineTimeoutMs / 1000)
                     .arg(backendLogPath()));
        }
    });
}

void BackendManager::onProcessReadyRead()
{
    if (!m_process)
        return;
    const QByteArray chunk = m_process->readAllStandardOutput();
    appendLog(chunk);
    if (m_ready)
        return;
    m_pendingBuffer += QString::fromUtf8(chunk);
    // dsh web prints "dsh web: <full url>"; since 0.1.2rc the URL carries an
    // auth token query param (e.g. http://127.0.0.1:3080/?token=...), which is
    // only available on this line, so capture the whole URL — not just the port.
    static const QRegularExpression re(QStringLiteral("dsh web:\\s*(https?://\\S+)"));
    const QRegularExpressionMatch match = re.match(m_pendingBuffer);
    if (match.hasMatch()) {
        m_url = QUrl(match.captured(1));
        emit statusChanged(tr("后端监听于 %1，等待就绪…").arg(sanitizeUrl(m_url)));
        startPolling(m_url, kReadyPollAttempts);
    }
    if (m_pendingBuffer.size() > (1 << 20))
        m_pendingBuffer = m_pendingBuffer.right(1 << 20);
}

void BackendManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    if (!m_ready && !m_shuttingDown) {
        fail(tr("Harness 后端进程已退出 (code %1)。日志: %2")
                 .arg(exitCode)
                 .arg(backendLogPath()));
    }
}

void BackendManager::onProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    if (!m_ready && !m_shuttingDown && m_process) {
        fail(tr("无法启动后端进程: %1。日志: %2")
                 .arg(m_process->errorString(), backendLogPath()));
    }
}

// --- readiness polling ----------------------------------------------------

void BackendManager::startPolling(const QUrl &url, int maxAttempts)
{
    m_url = url;
    m_pollAttempts = 0;
    m_maxPollAttempts = maxAttempts;
    connect(&m_pollTimer, &QTimer::timeout, this, &BackendManager::pollTick);
    m_pollTimer.start(kPollIntervalMs);
    pollTick();
}

void BackendManager::pollTick()
{
    if (m_shuttingDown || m_ready || m_failed)
        return;
    ++m_pollAttempts;
    if (m_pollAttempts > m_maxPollAttempts) {
        if (m_pollFallbackToSpawn) {
            // The service did not come up in time; degrade to a direct spawn.
            m_pollFallbackToSpawn = false;
            m_pollTimer.stop();
            spawnBackend();
            return;
        }
        fail(tr("等待 Harness 服务就绪超时 (%1)。日志: %2")
                 .arg(sanitizeUrl(m_url), backendLogPath()));
        return;
    }
    if (m_pollAttempts % 10 == 1) {
        emit statusChanged(tr("等待 Harness 服务就绪 (%1s)…")
                               .arg((m_pollAttempts * kPollIntervalMs) / 1000));
    }
    probeUrl(m_url, kReadyPoll);
}

void BackendManager::fail(const QString &detail)
{
    if (m_failed)
        return;
    m_failed = true;
    m_pollTimer.stop();
    if (m_process && m_spawned && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    emit statusChanged(detail);
    emit backendFailed(detail);
}

void BackendManager::shutdown()
{
    m_shuttingDown = true;
    m_pollTimer.stop();
    if (m_process && m_spawned && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}
