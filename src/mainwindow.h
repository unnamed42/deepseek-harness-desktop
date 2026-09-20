// mainwindow.h — DeepSeek Harness desktop wrapper
#pragma once

#include <QMainWindow>
#include <QUrl>

class BackendManager;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QWebEngineProfile;
class QWebEngineView;
class QWebEngineNotification;
class QSystemTrayIcon;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(BackendManager *backend, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onBackendStatus(const QString &message);
    void onBackendReady(const QUrl &url);
    void onBackendFailed(const QString &detail);
    void onLoadProgress(int progress);
    void onLoadFinished(bool ok);
    void retryLoad();
public slots:
    void focusWindow();

private:
    void buildSplash();
    void buildWebView();
    void loadBackendUrl();
    void saveWindowState();
    void restoreWindowState();
    void addShortcuts();
    void showNotification(QWebEngineNotification *notification);

    BackendManager *m_backend = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_splashStatus = nullptr;
    QLabel *m_splashError = nullptr;
    QPushButton *m_retryButton = nullptr;
    QProgressBar *m_splashProgress = nullptr;
    QWebEngineView *m_webView = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QUrl m_url;
    int m_reloadAttempts = 0;
};
