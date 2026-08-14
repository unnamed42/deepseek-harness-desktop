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
constexpr int kProbeTimeoutMs = 2500;   // per-request transfer timeout
constexpr int kPollIntervalMs = 300;    // readiness poll interval
constexpr int kUrlLineTimeoutMs = 20000; // wait for the printed URL line
constexpr int kReadyPollAttempts = 300;  // ~90 s while our backend boots
constexpr int kDirectPollAttempts = 1200; // ~6 min for a user-managed URL

const char *kProbeExisting = "probe-existing";
const char *kReadyPoll = "ready-poll";

QString dshEntryIn(const QString &dir)
{
    if (dir.isEmpty())
        return {};
    const QString entry = dir + QStringLiteral("/node_modules/@deepseek-ai/dsh/lib/bin.js");
    return QFileInfo::exists(entry) ? entry : QString();
}

QString detectRuntimeDir()
{
    const QStringList candidates = {
        qEnvironmentVariable("DSH_DESKTOP_RUNTIME_DIR"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/runtime"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../runtime"),
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../../lib/deepseek-harness-desktop/runtime"),
        QStringLiteral("/usr/lib/deepseek-harness-desktop/runtime"),
        QStringLiteral("/usr/local/lib/deepseek-harness-desktop/runtime"),
        QStringLiteral("/opt/deepseek-harness-desktop/runtime"),
    };
    for (const QString &candidate : candidates) {
        if (!dshEntryIn(candidate).isEmpty())
            return QDir(candidate).absolutePath();
    }
    return {};
}

QString nodeExecutable(const QString &overridePath)
{
    if (!overridePath.isEmpty() && QFileInfo(overridePath).isExecutable())
        return overridePath;
    const QString envPath = qEnvironmentVariable("DSH_DESKTOP_NODE");
    if (!envPath.isEmpty() && QFileInfo(envPath).isExecutable())
        return envPath;
    return QStandardPaths::findExecutable(QStringLiteral("node"));
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
    m_runtimeDir = detectRuntimeDir();
    appendLog(QByteArray("\n==== ")
              + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
              + QByteArray(" launch ====\n"));

    if (!directUrl.isEmpty()) {
        m_url = QUrl::fromUserInput(directUrl);
        emit statusChanged(tr("正在连接 %1 …").arg(m_url.toString()));
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
                emit statusChanged(tr("已连接到本地 Harness 服务: %1").arg(m_url.toString()));
                emit backendReady(m_url);
                return;
            }
            // Some unrelated service owns that port; never hijack it.
        }
        spawnBackend();
    } else if (kind == QLatin1String(kReadyPoll)) {
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            m_ready = true;
            m_pollTimer.stop();
            emit statusChanged(tr("Harness 服务已就绪: %1").arg(m_url.toString()));
            emit backendReady(m_url);
        }
        // otherwise the poll timer keeps going
    }
}

void BackendManager::spawnBackend()
{
    if (m_shuttingDown || m_failed)
        return;

    const QString node = nodeExecutable(nodeOverride);
    if (node.isEmpty()) {
        fail(tr("未找到 Node.js，无法启动 Harness 后端。请安装 nodejs 包，或通过 "
                "DSH_DESKTOP_NODE 指定 node 可执行文件。"));
        return;
    }

    QString program = node;
    QStringList arguments;
    const QString entry = dshEntryIn(m_runtimeDir);
    if (!entry.isEmpty()) {
        arguments << entry;
    } else {
        const QString dsh = QStandardPaths::findExecutable(QStringLiteral("dsh"));
        if (dsh.isEmpty()) {
            fail(tr("未找到捆绑的 Harness 运行时，PATH 中也没有 dsh 命令。"));
            return;
        }
        arguments << dsh;
    }
    arguments << QStringLiteral("web") << QStringLiteral("--host") << QStringLiteral("127.0.0.1")
              << QStringLiteral("--port") << QStringLiteral("0");

    m_spawned = true;
    m_pendingBuffer.clear();
    emit statusChanged(tr("正在启动 DeepSeek Harness 后端…"));

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &BackendManager::onProcessReadyRead);
    connect(m_process, &QProcess::finished, this, &BackendManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &BackendManager::onProcessErrorOccurred);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(QDir::homePath());
    m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process->setProgram(program);
    m_process->setArguments(arguments);
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
    static const QRegularExpression re(
        QStringLiteral("dsh web:\\s*(http://127\\.0\\.0\\.1:\\d+)"));
    const QRegularExpressionMatch match = re.match(m_pendingBuffer);
    if (match.hasMatch()) {
        m_url = QUrl(match.captured(1));
        emit statusChanged(tr("后端监听于 %1，等待就绪…").arg(m_url.toString()));
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
        fail(tr("等待 Harness 服务就绪超时 (%1)。日志: %2")
                 .arg(m_url.toString(), backendLogPath()));
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
