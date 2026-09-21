// mainwindow.cpp — DeepSeek Harness desktop wrapper
#include "mainwindow.h"

#include "backendmanager.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWebEngineNavigationRequest>
#include <QWebEnginePermission>
#include <QWebEngineNotification>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

#ifdef __linux__
#include <QDBusMessage>
#include <QDBusConnection>
#include <QList>
#include <QVariant>
#endif

#include <memory>

namespace {

inline bool isWebScheme(const QUrl &url)
{
    return url.scheme() == QLatin1String("http")
        || url.scheme() == QLatin1String("https");
}

inline bool isExternalUrl(const QUrl &url, const QUrl &backendBase)
{
    if (!isWebScheme(url) || url.isEmpty())
        return false;
    return url.host() != backendBase.host()
        || (url.port(-1) != backendBase.port(-1) && url.port(-1) != -1);
}

inline QIcon deepseekIcon(int size = 256) {
    return QIcon::fromTheme("deepseek-harness-desktop", QIcon(QString(":/icons/app-%1.png").arg(size)));
}

// Page that routes popup windows (target=_blank / window.open) and ordinary
// external links to the system browser instead of navigating inside the shell.
class AppWebPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;
    QUrl backendBase;

protected:

    QWebEnginePage *createWindow(WebWindowType type) override
    {
        Q_UNUSED(type);
        // Route popup windows (target=_blank / window.open) to the system
        // browser. The returned page must share THIS page's profile: creating
        // a fresh QWebEngineView would come with its own off-the-record
        // profile and Chromium then fails to adopt the new window's content
        // ("Can not adopt content from a different WebEngineProfile").
        // So we return a throwaway page of the same profile that forwards the
        // first real URL and then destroys itself.
        auto *page = new QWebEnginePage(profile(), this);
        connect(page, &QWebEnginePage::navigationRequested, page, [page](QWebEngineNavigationRequest &request) {
            auto url = request.url();
            if (isWebScheme(url) && !url.isEmpty())
                QDesktopServices::openUrl(url);
            request.reject();
            page->deleteLater();
        });
        return page;
    }
};

} // namespace

MainWindow::MainWindow(BackendManager *backend, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend)
{
    setWindowTitle(tr("DeepSeek Harness"));
    auto windowIcon = deepseekIcon(64);
    setWindowIcon(windowIcon);
    setMinimumSize(860, 600);
    resize(1280, 860);

    // setup system tray
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        m_trayIcon->setIcon(windowIcon);
        m_trayIcon->setToolTip(tr("Deepseek Harness 桌面版"));

        auto menu = new QMenu(this);
        auto quitAction = new QAction(tr("退出"));
        auto showAction = new QAction(tr("显示"));
        menu->addAction(showAction);
        menu->addSeparator();
        menu->addAction(quitAction);
        m_trayIcon->setContextMenu(menu);

        connect(qApp, &QCoreApplication::aboutToQuit, m_trayIcon, &QSystemTrayIcon::hide);
        connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
        connect(showAction, &QAction::triggered, this, &MainWindow::focusWindow);
        connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &MainWindow::focusWindow);
        connect(m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
            if(reason == QSystemTrayIcon::Trigger)
                focusWindow();
        });

        m_trayIcon->show();
    }

#ifdef __linux__
    // setup linux specific desktop notification
    QDBusConnection::sessionBus().connect(
        "",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "ActionInvoked",
        this,
        SLOT(onActionInvoked(uint, QString))
    );
    QDBusConnection::sessionBus().connect(
        "",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "ActivationToken",
        this,
        SLOT(onActivationToken(uint, QString))
    );
    QDBusConnection::sessionBus().connect(
        "",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "NotificationClosed",
        this,
        SLOT(onNotificationClosed(uint, uint))
    );
#endif

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    buildSplash();
    addShortcuts();
    restoreWindowState();

    connect(m_backend, &BackendManager::statusChanged, this, &MainWindow::onBackendStatus);
    connect(m_backend, &BackendManager::backendReady, this, &MainWindow::onBackendReady);
    connect(m_backend, &BackendManager::backendFailed, this, &MainWindow::onBackendFailed);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildSplash()
{
    auto *splash = new QWidget(this);
    auto *layout = new QVBoxLayout(splash);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(10);
    layout->setContentsMargins(48, 48, 48, 48);

    auto *icon = new QLabel(splash);
    auto windowIcon = deepseekIcon();
    icon->setPixmap(windowIcon.pixmap(QSize(128, 128)));
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    auto *title = new QLabel(tr("DeepSeek Harness"), splash);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *subtitle = new QLabel(tr("本地优先的 AI Agent 工作台"), splash);
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(8);

    m_splashStatus = new QLabel(splash);
    m_splashStatus->setAlignment(Qt::AlignCenter);
    m_splashStatus->setWordWrap(true);
    layout->addWidget(m_splashStatus);

    m_splashProgress = new QProgressBar(splash);
    m_splashProgress->setRange(0, 100);
    m_splashProgress->setValue(0);
    m_splashProgress->setTextVisible(false);
    m_splashProgress->setMaximumWidth(320);
    m_splashProgress->hide();
    layout->addWidget(m_splashProgress, 0, Qt::AlignHCenter);

    m_splashError = new QLabel(splash);
    m_splashError->setAlignment(Qt::AlignCenter);
    m_splashError->setWordWrap(true);
    m_splashError->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_splashError->hide();
    layout->addWidget(m_splashError);

    m_retryButton = new QPushButton(tr("重新加载"), splash);
    m_retryButton->setMaximumWidth(160);
    m_retryButton->hide();
    connect(m_retryButton, &QPushButton::clicked, this, &MainWindow::retryLoad);
    layout->addWidget(m_retryButton, 0, Qt::AlignHCenter);

    m_stack->addWidget(splash);
}

void MainWindow::addShortcuts()
{
    const auto addShortcut = [this](const QKeySequence &sequence, const auto &action) {
        auto *shortcut = new QShortcut(sequence, this);
        connect(shortcut, &QShortcut::activated, this, action);
    };

    addShortcut(QKeySequence::Refresh, [this] {
        if (m_webView)
            m_webView->reload();
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), [this] {
        if (m_webView)
            m_webView->reload();
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] {
        if (m_webView)
            m_webView->setZoomFactor(qMin(3.0, m_webView->zoomFactor() + 0.1));
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] {
        if (m_webView)
            m_webView->setZoomFactor(qMax(0.5, m_webView->zoomFactor() - 0.1));
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [this] {
        if (m_webView)
            m_webView->setZoomFactor(1.0);
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { close(); });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), [] { QCoreApplication::quit(); });
}

void MainWindow::onBackendStatus(const QString &message)
{
    if (m_splashStatus)
        m_splashStatus->setText(message);
}

void MainWindow::onBackendReady(const QUrl &url)
{
    m_url = url;
    buildWebView();
    loadBackendUrl();
}

void MainWindow::onBackendFailed(const QString &detail)
{
    if (m_splashStatus)
        m_splashStatus->setText(detail);
    if (m_splashError) {
        m_splashError->setText(
            tr("后端未能就绪。请确认已安装 deepseek-harness-git（提供 dsh 命令），"
               "详见日志 %1，或先手动运行 “dsh web”。")
                .arg(m_backend->backendLogPath()));
        m_splashError->show();
    }
    if (m_retryButton && !m_url.isEmpty())
        m_retryButton->show();
}

void MainWindow::buildWebView()
{
    if (m_webView)
        return;

    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    m_profile = new QWebEngineProfile(QStringLiteral("harness-desktop"), this);
    m_profile->setPersistentStoragePath(dataRoot + QStringLiteral("/web-storage"));
    m_profile->setCachePath(dataRoot + QStringLiteral("/web-cache"));
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    auto *page = new AppWebPage(m_profile, nullptr);
    page->backendBase = m_url;

    m_webView = new QWebEngineView;
    m_webView->setPage(page);
    connect(m_webView, &QWebEngineView::loadProgress, this, &MainWindow::onLoadProgress);
    connect(m_webView, &QWebEngineView::loadFinished, this, &MainWindow::onLoadFinished);
    // allow desktop notifications
    connect(page, &QWebEnginePage::permissionRequested, [](QWebEnginePermission permission) {
        if (permission.permissionType() == QWebEnginePermission::PermissionType::Notifications)
            permission.grant();
    });
    m_profile->setNotificationPresenter([this](std::unique_ptr<QWebEngineNotification> notification) {
        showNotification(notification.get());
    });

    m_stack->addWidget(m_webView);
}

void MainWindow::loadBackendUrl()
{
    if (!m_webView)
        return;
    m_splashProgress->setValue(0);
    m_splashProgress->show();
    m_splashError->hide();
    m_retryButton->hide();
    m_webView->load(m_url);
}

void MainWindow::onLoadProgress(int progress)
{
    m_splashProgress->setValue(progress);
}

void MainWindow::onLoadFinished(bool ok)
{
    if (ok) {
        m_reloadAttempts = 0;
        m_splashProgress->hide();
        m_stack->setCurrentWidget(m_webView);
        return;
    }
    if (m_reloadAttempts < 3) {
        ++m_reloadAttempts;
        m_splashStatus->setText(
            tr("界面加载失败，正在重试 (%1/3)…").arg(m_reloadAttempts));
        QTimer::singleShot(1200, this, &MainWindow::loadBackendUrl);
        return;
    }
    m_splashProgress->hide();
    m_splashStatus->setText(tr("界面加载失败。"));
    m_splashError->setText(tr("多次加载失败，请确认后端服务仍在运行，然后点击“重新加载”。"));
    m_splashError->show();
    m_retryButton->show();
}

void MainWindow::retryLoad()
{
    if (m_url.isEmpty()) {
        QMessageBox::information(this, tr("DeepSeek Harness"),
                                 tr("后端尚未就绪，请重新启动应用。"));
        return;
    }
    buildWebView();
    loadBackendUrl();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    m_backend->shutdown();
    event->accept();
}

void MainWindow::changeEvent(QEvent *event)
{
    if(m_webView && event->type() == QEvent::WindowStateChange)
    {
        if(isMinimized() || isHidden())
            m_webView->page()->setVisible(false);
        else
            m_webView->page()->setVisible(true);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::showNotification(QWebEngineNotification *notification)
{
#ifndef __linux__
    if(!m_trayIcon)
        return;
    auto iconImage = notification->icon();
    m_trayIcon->showMessage(
        notification->title(),
        notification->message(),
        iconImage.isNull() ? deepseekIcon(128) : QPixmap::fromImage(iconImage),
        5000
    );
#else
    auto msg = QDBusMessage::createMethodCall(
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify"
    );

    QList<QVariant> args;
    args.append("Deepseek Harness Desktop"); // app name
    args.append(uint(0)); // replaces_id
    args.append("deepseek-harness-desktop"); // app icon
    args.append(notification->title()); // summary
    args.append(notification->message()); // body
    args.append(QStringList({"default", tr("打开")})); // actions
    args.append(QVariantMap()); // hints (empty)
    args.append(int(-1)); // default timeout

    msg.setArguments(args);

    auto reply = QDBusConnection::sessionBus().call(msg);
    if(reply.type() == QDBusMessage::ErrorMessage)
        qDebug() << "QDBus error sending notification:" << reply.errorMessage();
#endif
}

void MainWindow::onActivationToken(uint id, const QString &token)
{
    m_activationTokens[id] = token;
}

void MainWindow::onActionInvoked(uint id, const QString &actionKey)
{
    QString token = m_activationTokens.take(id);
    if (!token.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
    focusWindow();
}

void MainWindow::onNotificationClosed(uint id, uint reason)
{
    m_activationTokens.remove(id);
}

void MainWindow::focusWindow()
{
    if (isHidden() || isMinimized()) {
        setWindowState(windowState() & ~Qt::WindowMinimized | Qt::WindowActive);
        show();
    }
    raise();
    activateWindow();
}
