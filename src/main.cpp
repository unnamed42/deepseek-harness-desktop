// main.cpp — DeepSeek Harness desktop wrapper entry point
#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QSocketNotifier>

#include <csignal>
#include <unistd.h>

#include "backendmanager.h"
#include "mainwindow.h"

namespace {
int g_signalPipe[2] = {-1, -1};

void handleUnixSignal(int)
{
    // async-signal-safe: wake the Qt event loop through a self-pipe
    const char byte = 1;
    if (g_signalPipe[1] != -1)
        (void)::write(g_signalPipe[1], &byte, 1);
}

void installSignalHandlers()
{
    if (::pipe(g_signalPipe) != 0)
        return;
    struct sigaction action {};
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("deepseek-harness-desktop"));
    QApplication::setApplicationDisplayName(QStringLiteral("DeepSeek Harness"));
    QApplication::setOrganizationName(QStringLiteral("deepseek"));
    QApplication::setOrganizationDomain(QStringLiteral("deepseek.com"));
    QApplication::setApplicationVersion(QStringLiteral(DSH_DESKTOP_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("deepseek-harness-desktop"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));

    if (::geteuid() == 0) {
        // The Chromium sandbox cannot run as root.
        const QByteArray existing = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
                existing.isEmpty() ? QByteArray("--no-sandbox") : existing + " --no-sandbox");
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DeepSeek Harness 桌面应用"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("优先复用的后端端口（默认 %1，0 表示总是新起后端）")
            .arg(BackendManager::defaultPort()),
        QStringLiteral("port"));
    const QCommandLineOption urlOption(QStringLiteral("url"),
                                       QStringLiteral("直接加载指定 URL，不自动探测/启动后端"),
                                       QStringLiteral("url"));
    const QCommandLineOption runtimeOption(QStringLiteral("runtime"),
                                           QStringLiteral("Harness 运行时目录（含 node_modules）"),
                                           QStringLiteral("dir"));
    const QCommandLineOption nodeOption(QStringLiteral("node"),
                                        QStringLiteral("node 可执行文件路径"),
                                        QStringLiteral("path"));
    parser.addOption(portOption);
    parser.addOption(urlOption);
    parser.addOption(runtimeOption);
    parser.addOption(nodeOption);
    parser.process(app);

    BackendManager backend;
    backend.preferredPort = parser.value(portOption);
    backend.directUrl = parser.value(urlOption);
    backend.runtimeOverride = parser.value(runtimeOption);
    backend.nodeOverride = parser.value(nodeOption);

    MainWindow window(&backend);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend,
                     &BackendManager::shutdown);

    // Graceful termination (session logout, systemd stop, Ctrl+C):
    // quit() runs the event loop teardown, which triggers aboutToQuit
    // and lets a spawned backend be shut down cleanly.
    installSignalHandlers();
    QSocketNotifier signalNotifier(g_signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app,
                     &QCoreApplication::quit);

    window.show();
    backend.start();

    return app.exec();
}
