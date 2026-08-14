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

namespace {

// Page that routes popup windows (target=_blank / window.open) for external
// hosts to the system browser instead of opening a new webview window.
class AppWebPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;
    QUrl backendBase;

protected:
    QWebEnginePage *createWindow(WebWindowType type) override
    {
        Q_UNUSED(type);
        // Route popup windows for external hosts to the system browser.
        auto *view = new QWebEngineView;
        view->setAttribute(Qt::WA_DeleteOnClose);
        connect(view, &QWebEngineView::urlChanged, this, [view, this](const QUrl &url) {
            const bool webScheme = url.scheme() == QLatin1String("http")
                || url.scheme() == QLatin1String("https");
            if (webScheme && !url.isEmpty()) {
                const bool external = url.host() != backendBase.host()
                    || (url.port(-1) != backendBase.port(-1) && url.port(-1) != -1);
                if (external)
                    QDesktopServices::openUrl(url);
            }
            view->close();
        });
        return view->page();
    }
};

} // namespace

MainWindow::MainWindow(BackendManager *backend, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend)
{
    setWindowTitle(tr("DeepSeek Harness"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    setMinimumSize(860, 600);
    resize(1280, 860);

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
    icon->setPixmap(QPixmap(QStringLiteral(":/icons/app-256.png"))
                        .scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
