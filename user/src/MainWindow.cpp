/**
 * @file MainWindow.cpp
 * 主窗口：协议收发、登录同步、网络/消息与会话 UI 部分实现。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ChoiceDialog.h"
#include "Dialog.h"
#include "SocketOnly.h"
#include "ClientConfigDefaults.h"
#include "CloseButtonUtils.h"
#include "SocketDoc.h"
#include "AvatarManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QTimer>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QCoreApplication>
#include "ClientConfigDefaults.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QPropertyAnimation>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

static const int kChatListSpacing = 4;       // 消息项之间统一间距
static const int kChatMessageRowMargin = 0;  // 消息行仅依赖 QListWidget::spacing，避免边界额外空隙
static const int kChatTimeRowHeight = 32;    // 时间戳行最小高度，防止 11pt 字体被截断

// 聊天消息分页大小（与 QQ/微信类似，先加载最近一页，向上滚动加载更早）
const int MainWindow::CHAT_PAGE_SIZE = 50;

// 选中状态样式
const QString MainWindow::BTN_SELECTED_STYLE = R"(
    QPushButton {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 153, 179, 0.4);
        border: 4px solid rgba(255, 153, 179, 1);
        border-radius: 10px;
        color: #222;
    }
    QPushButton:hover {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 182, 193, 0.5);
        border: 4px solid rgba(255, 182, 193, 1);
    }
    QPushButton:pressed {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 120, 160, 0.5);
        border: 4px solid rgba(255, 120, 160, 1);
    }
)";
// 未选中状态样式
const QString MainWindow::BTN_UNSELECTED_STYLE = R"(
    QPushButton {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 153, 179, 0.25);
        border: 4px solid rgba(255, 153, 179, 0.25);
        border-radius: 10px;
        color: #666;
    }
    QPushButton:hover {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 182, 193, 0.5);
        border: 4px solid rgba(255, 182, 193, 1);
        color: #222;
    }
    QPushButton:pressed {
        font: 12pt "Microsoft YaHei UI";
        background-color: rgba(255, 120, 160, 0.5);
        border: 4px solid rgba(255, 120, 160, 1);
        color: #222;
    }
)";

const QString MainWindow::BTN_SELECTED_STYLE_SEC = R"(
    QPushButton {
        font: 13pt 'Microsoft YaHei UI';
        background-color: rgb(140, 180, 255);
        border: 2px solid rgb(80, 140, 255);
        border-radius: 8px;
    }
)";
const QString MainWindow::BTN_UNSELECTED_STYLE_SEC = R"(
    QPushButton {
        font: 13pt 'Microsoft YaHei UI';
        background: transparent;
        border: none;
    }
    QPushButton:hover {
        font: 13pt 'Microsoft YaHei UI';
        background: rgba(140, 180, 255, 0.3);
        border-radius: 10px;
    }
)";

// 获取配置文件路径（基于 ClientConfigDefaults::dataDir()）。
static QString settingsIniPath()
{
    return ClientConfigDefaults::settingsIniPath();
}

// 将字符串转为安全的配置键名
static QString safeKeyPart(QString s)
{
    if (s.isEmpty()) return "default";
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (!(c.isLetterOrNumber() || c == '_' || c == '-')) {
            s[i] = '_';
        }
    }
    return s;
}

// 获取用户主题目录路径
static QString themeDirPath(const QString &account)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QDir::separator() + "theme" + QDir::separator() + safeKeyPart(account);
}

// 构造函数
MainWindow::MainWindow(const QString accountNumber, QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_friendModel(nullptr)
{
    m_myInfo.account = accountNumber;
    ui->setupUi(this);
    // 初始化所有按钮图标与左侧栏水印
    initIcons();
    // 加载主题设置
    loadThemeSettings();
    // 初始化窗口
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    // 关闭按钮统一样式
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    // 设置好友列表模型
    setupFriendList();
    // 设置聊天列表模型
    setupTalkList();
    // 设置左下角按钮菜单
    setupMenu();
    // 初始化托盘（右上角关闭默认最小化到托盘）
    initTrayIcon();
    // 设置云盘页面
    setupCloudPage();
    // 设置AI页面
    setupAiPage();
    // 设置语音相关设置
    setupAudioSettings();
    // 在本地建立或初始化该用户的数据库
    QString dbName = createUserDirs(m_myInfo.account);
    // 初始化线程安全队列
    m_msgQueue = new ThreadSafeQueue<MessageData>();
    m_talksQueue = new ThreadSafeQueue<TalksData>();
    // 初始化并启动数据库工作线程
    m_dbWorkerThread = new DbWorkerThread(m_msgQueue, m_talksQueue, dbName, this);
    m_dbWorkerThread->start();
    // 连接聊天列表右键菜单和窗体的信号槽
    connect(ui->list_talks, &TalkList::choiceDone, this, &MainWindow::listtalkChoice);
    // 设置聊天页面先不显示好友名字
    ui->lab_friendname->setText("");
    // 设置聊天页面先不可输入
    ui->edit_input->setEnabled(false);
    // 显式绑定左侧导航，避免仅依赖自动槽连接导致点击无响应
    connect(ui->but_chat, &QPushButton::clicked, this, &MainWindow::on_but_chat_clicked, Qt::UniqueConnection);
    connect(ui->but_friends, &QPushButton::clicked, this, &MainWindow::on_but_friends_clicked, Qt::UniqueConnection);
    connect(ui->but_cloud, &QPushButton::clicked, this, &MainWindow::on_but_cloud_clicked, Qt::UniqueConnection);
    connect(ui->but_ai, &QPushButton::clicked, this, &MainWindow::on_but_ai_clicked, Qt::UniqueConnection);
    // 启动默认显示聊天页，确保 AI 按钮切页有可见反馈
    ui->pages->setCurrentIndex(0);
    on_pages_currentChanged(0);
    // 连接聊天输入框的回车和提交
    connect(ui->edit_input, &EnterTextEdit::enterKey, [this] (){
        if (!SocketOnly::instance().isConnected())
            return;
        sendMessage();
    });
    // 连接保存文件完成后弹窗
    connect(this, &MainWindow::saveDone, [this] (const QString &status){
        m_savePath = "";
        clearDownLoad();
        finishCloudDriveDownload(status);
        Dialog* dialog = new Dialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->transText(status);
        dialog->show();
    });
    // 禁用 edit_show2 的文本编辑光标（仅作为布局容器使用）
    ui->edit_show2->setReadOnly(true);
    ui->edit_show2->setTextInteractionFlags(Qt::NoTextInteraction);
    // 创建右边用户信息主布局
    m_newLayout = new QVBoxLayout(ui->edit_show2);
    m_newLayout->setAlignment(Qt::AlignCenter); // 主布局居中
    // 设置消息框与好友列表不显示左右的滑块
    ui->list_talks->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->list_friends->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 连接左上角头像和弹出修改信息窗口的信号槽
    connect(ui->lab_avator, &LabelAva::changeInfo, this, &MainWindow::changeInfo);
    // 连接socket的信号槽
    connect(&SocketOnly::instance(), &SocketOnly::dataReceived,
            this, &MainWindow::onReadyRead, Qt::QueuedConnection);
    // 主连接断开：清理 AI 会话令牌与「分析中」状态，关闭等待框。
    connect(&SocketOnly::instance(), &SocketOnly::socketDisconnected,
            this, [this]() {
                m_aiAnalysisInProgress = false;
                m_aiSessionToken.clear();
                if (m_aiWaitDialog) {
                    m_aiWaitDialog->close();
                }
                if (m_didFirstHavelogin) {
                    m_needResyncTalkPagesAfterReconnect = true;
                    applyNetworkOfflineUi();
                    showReconnectTipDialogIfNeeded();
                }
            },
            Qt::QueuedConnection);
    connect(&SocketOnly::instance(), &SocketOnly::socketConnected,
            this, &MainWindow::onMainSocketConnected, Qt::QueuedConnection);
    connect(&SocketOnly::instance(), &SocketOnly::socketError,
            this, &MainWindow::onMainSocketError, Qt::QueuedConnection);
    connect(&SocketOnly::instance(), &SocketOnly::reconnectFailed,
            this, &MainWindow::onMainSocketReconnectFailed, Qt::QueuedConnection);

    connect(&SocketDocRead::instance(), &SocketDocRead::socketDisconnected,
            this, [this]() {
                cancelDocumentDownloadDueToNetwork(QStringLiteral("下载连接已断开"));
            },
            Qt::QueuedConnection);
    connect(&SocketDocRead::instance(), &SocketDocRead::socketError,
            this, [this](const QString &) {
                cancelDocumentDownloadDueToNetwork(QStringLiteral("下载连接异常"));
            },
            Qt::QueuedConnection);

    m_loginResyncDebounce = new QTimer(this);
    m_loginResyncDebounce->setSingleShot(true);
    m_loginResyncDebounce->setInterval(250);
    connect(m_loginResyncDebounce, &QTimer::timeout, this, [this]() {
        if (m_myInfo.account.isEmpty())
            return;
        if (!SocketOnly::instance().isConnected())
            return;
        havelogin(m_myInfo.account);
    });

    m_pendingMessageAckSweepTimer = new QTimer(this);
    m_pendingMessageAckSweepTimer->setInterval(ClientConfigDefaults::defaultPendingMessageAckSweepIntervalMs());
    connect(m_pendingMessageAckSweepTimer, &QTimer::timeout, this, &MainWindow::sweepPendingOutboundAckTimeouts);

    // 通过定时器延迟
    QTimer::singleShot(150, this, [this]() {
        // 发送已经登录的信号
        havelogin(m_myInfo.account);// 收到回复后依次调用加载消息列表 加载聊天记录
        m_didFirstHavelogin = true;
        // 设置按钮位置
        positionSendButton();
        // 窗口操作
        raise();
        activateWindow();
    });
    // 初始化页面显示
    ui->but_friends->click();
    // 设置音效
    m_sound = new QSoundEffect(this);
    m_sound->setSource(QUrl::fromLocalFile(":/sound/ring.wav"));
    m_sound->setVolume(1.0);
    // 设置更新聊天列表库的定时器
    m_flushDbTimer = new QTimer(this);
    m_flushDbTimer->setInterval(900000); // 900000毫秒 = 15分钟
    connect(m_flushDbTimer, &QTimer::timeout, this, &MainWindow::onTimerFlushTalksToDb);
}

// 析构函数
MainWindow::~MainWindow()
{
    // 刷新消息列表库
    onTimerFlushTalksToDb();

    // 处理数据库上传队列
    if (m_dbWorkerThread) {
        m_dbWorkerThread->stop();
        m_dbWorkerThread->wait(); // 等待线程完全退出
        delete m_dbWorkerThread;
    }
    if (m_msgQueue) {
        delete m_msgQueue;
    }
    if (m_talksQueue) {
        delete m_talksQueue;
    }

    // 如果有文件在传 删除本地聊天记录
    QString sender = m_uploadFile["record_account"].toString();
    QString receiver = m_uploadFile["record_receiver"].toString();
    QString filename = m_uploadFile["record_filename"].toString();
    QString timestampVar = m_uploadFile["record_timestamp"].toString();
    if (!(sender.isEmpty() || receiver.isEmpty() || filename.isEmpty() || timestampVar.isEmpty())) {
        QSqlQuery qry(m_db);
        qry.prepare(R"(
        DELETE FROM messages
        WHERE sender = :sender
          AND receiver = :receiver
          AND messagetype = 'document'
          AND message = :filename
          AND server_timestamp = :server_timestamp
        )");

        qry.bindValue(":sender", sender);
        qry.bindValue(":receiver", receiver);
        qry.bindValue(":filename", filename);
        qry.bindValue(":server_timestamp", timestampVar);
        bool execSuccess = qry.exec();
        if (execSuccess) {
            int affectedRows = qry.numRowsAffected();
            if (affectedRows > 0) {
                qDebug() << "[删除记录成功] 已删除符合条件的messages记录："
                         << "发送方=" << sender
                         << "接收方=" << receiver
                         << "文件名=" << filename
                         << "时间戳=" << timestampVar
                         << "，受影响行数：" << affectedRows;
            }
        }
    } else {
        qDebug() << "[删除记录跳过] 关键业务字段（发送方/接收方/文件名/时间戳）为空或无效，无需执行删除操作";
    }

    if (m_db.isValid() && m_db.isOpen()) {
        m_db.close();
        qDebug() << "SQLite 连接已关闭：" << m_db.connectionName();
    }
    if (m_db.isValid()) {
        QString connName = m_db.connectionName();
        m_db = QSqlDatabase();  // 销毁引用后再 removeDatabase，避免 "connection is still in use"
        QSqlDatabase::removeDatabase(connName);
        qDebug() << "已移除连接注册表：" << connName;
    }
    delete ui;
}

// 初始化所有按钮图标与左侧栏水印
void MainWindow::initIcons()
{
    // 导航按钮图标
    ui->but_chat->setIcon(QIcon(":/pictures/icon_chat.png"));
    ui->but_chat->setIconSize(QSize(24, 24));
    ui->but_friends->setIcon(QIcon(":/pictures/icon_friends.png"));
    ui->but_friends->setIconSize(QSize(24, 24));
    ui->but_set->setIcon(QIcon(":/pictures/icon_settings.png"));
    ui->but_set->setIconSize(QSize(24, 24));
    ui->but_ai->setIcon(QIcon(":/pictures/icon_ai.png"));
    ui->but_ai->setIconSize(QSize(24, 24));
    ui->but_cloud->setIcon(QIcon(":/pictures/icon_cloud.png"));
    ui->but_cloud->setIconSize(QSize(24, 24));

    // ============================================================
    // ★ 水印链接 — 请勿删除！
    // ============================================================
    {
        auto makeWatermark = [this](const QString &iconPath, const QString &url, const QString &tipString) {
            QPushButton *btn = new QPushButton(ui->centralwidget);
            btn->setFixedSize(ui->but_set->size());
            btn->setIcon(QIcon(iconPath));
            btn->setIconSize(ui->but_set->iconSize());
            btn->setStyleSheet(
                "QPushButton {"
                " border:none; background:transparent;"
                " font: 7pt 'Microsoft YaHei UI'; color:#aaa;"
                "}"
                "QPushButton:hover {"
                " background:rgba(255,153,179,0.3); border-radius:8px; color:#666;"
                "}"
                "QToolTip {"
                "font: 13pt 'Microsoft YaHei UI';"
                "color: #222;   "              
                "background: rgb(255, 250, 252);"
                "border: 2px solid rgba(255, 153, 179, 1); "
                "border-radius: 0px;"
                "text-align: center;}"
            );
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(tipString + "\n" + url);
            QObject::connect(btn, &QPushButton::clicked, [url]() {
                QApplication::clipboard()->setText(url);
                QDesktopServices::openUrl(QUrl(url));
            });
            return btn;
        };

        int idx = ui->verticalLayout->indexOf(ui->but_set);
        if (idx >= 0) {
            QVBoxLayout *linkCol = new QVBoxLayout;
            linkCol->setAlignment(Qt::AlignCenter);
            linkCol->setSpacing(4);
            linkCol->addWidget(makeWatermark(":/pictures/icon_github.png",
                                              "https://github.com/Qiuyue0125",
                                              "作者GitHub链接如下:"));
            linkCol->addWidget(makeWatermark(":/pictures/icon_bilibili.png",
                                              "https://space.bilibili.com/112381760",
                                              "作者哔哩哔哩链接如下:"));
            ui->verticalLayout->insertLayout(idx, linkCol);
        }
    }
    // ============================================================

    // 窗口控制按钮（统一26x26）
    ui->but_minwindow->setIcon(QIcon(":/pictures/icon_min.png"));
    ui->but_minwindow->setIconSize(QSize(12, 12));
    ui->but_minwindow->setFixedSize(26, 26);
    ui->but_minwindow->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: grey; border-radius: 4px; }");
    ui->but_maxwindow->setIcon(QIcon(":/pictures/icon_max.png"));
    ui->but_maxwindow->setIconSize(QSize(12, 12));
    ui->but_maxwindow->setFixedSize(26, 26);
    ui->but_maxwindow->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: grey; border-radius: 4px; }");
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    ui->but_deletewindow->setFixedSize(26, 26);
    ui->but_logo->setIcon(QIcon(":/pictures/icon_logo.png"));
    ui->but_logo->setIconSize(QSize(18, 18));
    ui->but_add0->setIcon(QIcon(":/pictures/icon_add_friend.png"));
    ui->but_add0->setIconSize(QSize(18, 18));

    // 聊天功能按钮
    ui->but_tool_sendpic->setIcon(QIcon(":/pictures/icon_send_pic.png"));
    ui->but_tool_sendpic->setIconSize(QSize(20, 20));
    ui->but_tool_sendfile->setIcon(QIcon(":/pictures/icon_send_file.png"));
    ui->but_tool_sendfile->setIconSize(QSize(20, 20));
    // 切换语音icon状态在 icon_send_audio_pause 函数内也有
    ui->but_tool_sendaudio->setIcon(QIcon(":/pictures/icon_send_audio_play.png"));
    ui->but_tool_sendaudio->setIconSize(QSize(20, 20));
    ui->but_upload->setIcon(QIcon(":/pictures/icon_upload.png"));
    ui->but_upload->setIconSize(QSize(18, 18));
    ui->but_download->setIcon(QIcon(":/pictures/icon_download.png"));
    ui->but_download->setIconSize(QSize(18, 18));
    
    // 好友页（搜索、添加好友按钮）
    ui->but_logo2->setIcon(QIcon(":/pictures/icon_logo.png"));
    ui->but_logo2->setIconSize(QSize(18, 18));
    ui->but_addfriends->setIcon(QIcon(":/pictures/icon_add_friend.png"));
    ui->but_addfriends->setIconSize(QSize(18, 18));

    // 记录上传/下载按钮的初始QSS，后续进度叠加时保持一致风格
    m_uploadBtnBaseStyle = ui->but_upload->styleSheet();
    m_downloadBtnBaseStyle = ui->but_download->styleSheet();
    clearButtonProgress(ui->but_upload, "无上传");
    clearButtonProgress(ui->but_download, "无下载");
}

// 切换语音按钮图标
void MainWindow::setAudioBtnRecording(bool recording)
{
    if (recording) {
        ui->but_tool_sendaudio->setIcon(QIcon(":/pictures/icon_send_audio_pause.png"));
        ui->but_tool_sendaudio->setText(QStringLiteral("松开发送"));
    } else {
        ui->but_tool_sendaudio->setIcon(QIcon(":/pictures/icon_send_audio_play.png"));
        ui->but_tool_sendaudio->setText(QStringLiteral("发送语音"));
    }
}

// 关闭事件
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_forceQuit && m_trayIcon && m_trayIcon->isVisible()) {
        event->ignore();
        if (m_trayHideAnimating) {
            return;
        }
        m_trayHideAnimating = true;
        QPropertyAnimation *fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(170);
        fade->setStartValue(windowOpacity());
        fade->setEndValue(0.0);
        connect(fade, &QPropertyAnimation::finished, this, [this, fade]() {
            Q_UNUSED(fade);
            hide();
            setWindowOpacity(1.0);
            m_trayHideAnimating = false;
            if (!m_trayTipShown) {
                m_trayTipShown = true;
                m_trayIcon->showMessage(QStringLiteral("速聊"),
                                        QStringLiteral("已最小化到系统托盘。可在托盘中恢复或退出。"),
                                        QSystemTrayIcon::Information, 2500);
            }
        });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }
    flushTalksCacheToDatabase();
    event->accept();
    deleteLater();
    QMainWindow::closeEvent(event);
}

// 初始化系统托盘图标与菜单
void MainWindow::initTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    if (m_trayIcon) return;

    m_trayIcon = new QSystemTrayIcon(this);
    QIcon trayIco = windowIcon();
    if (trayIco.isNull()) {
        trayIco = QIcon(":/pictures/suliao.png");
    }
    m_trayIcon->setIcon(trayIco);
    m_trayIcon->setToolTip(QStringLiteral("速聊"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->setStyleSheet(
        "QMenu {"
        " background-color: #ffffff;"
        " border: 1px solid #dfe3ea;"
        " border-radius: 8px;"
        " padding: 6px;"
        "}"
        "QMenu::item {"
        " text-align: center;"
        " padding: 8px 18px;"
        " margin: 2px 0;"
        " border-radius: 6px;"
        " color: #1f2937;"
        "}"
        "QMenu::item:disabled {"
        " color: #4b5563;"
        " background-color: transparent;"
        "}"
        "QMenu::item:disabled:selected {"
        " color: #4b5563;"
        " background-color: transparent;"
        "}"
        "QMenu::item:selected {"
        " background-color: #eaf3ff;"
        " color: #0b57d0;"
        "}"
    );
    m_trayUserAction = m_trayMenu->addAction(QStringLiteral("未登录"));
    if (m_trayUserAction) {
        m_trayUserAction->setEnabled(false);
    }
    m_trayMenu->addSeparator();
    QAction *showAction = m_trayMenu->addAction(QStringLiteral("显示主窗口"));
    QAction *exitAction = m_trayMenu->addAction(QStringLiteral("退出"));
    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    connect(exitAction, &QAction::triggered, this, &MainWindow::quitApplicationFromUi);
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this]() {
        if (!m_trayUserAction) return;
        const QString account = m_myInfo.account.trimmed();
        const QString name = m_myInfo.name.trimmed();
        if (!account.isEmpty() && !name.isEmpty()) {
            m_trayUserAction->setText(QStringLiteral("%1 (%2)").arg(name, account));
        } else if (!account.isEmpty()) {
            m_trayUserAction->setText(QStringLiteral("账号: %1").arg(account));
        } else {
            m_trayUserAction->setText(QStringLiteral("未登录"));
        }
    });

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Context) {
                    if (m_trayMenu) {
                        const QPoint p = QCursor::pos();
                        const QSize ms = m_trayMenu->sizeHint();
                        m_trayMenu->popup(QPoint(p.x() - ms.width() / 2 + 18, p.y() - ms.height() - 8));
                    }
                    return;
                }
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    showFromTray();
                }
            });
    m_trayIcon->show();
}

// 由显式退出入口触发，执行真正退出
void MainWindow::quitApplicationFromUi()
{
    m_forceQuit = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    close();
    QApplication::quit();
}

// 从托盘恢复窗口
void MainWindow::showFromTray()
{
    QPoint centerPos(900, 540);
    if (QScreen *primary = QGuiApplication::primaryScreen()) {
        centerPos = primary->geometry().center();
    }

    QSize winSize = normalGeometry().size();
    if (winSize.width() <= 0 || winSize.height() <= 0) {
        winSize = size();
    }
    if (winSize.width() <= 0 || winSize.height() <= 0) {
        winSize = QSize(900, 640);
    }

    const QPoint topLeft(centerPos.x() - winSize.width() / 2, centerPos.y() - winSize.height() / 2);
    showNormal();
    move(topLeft);
    QTimer::singleShot(0, this, [this, topLeft]() {
        move(topLeft);
        raise();
        activateWindow();
    });
    setWindowOpacity(1.0);
    m_trayHideAnimating = false;
    raise();
    activateWindow();
}

// 从托盘点击位置恢复窗口（点击点作为窗口左下角，并向右上偏移）
void MainWindow::showFromTrayAt(const QPoint &globalClickPos)
{
    QSize winSize = normalGeometry().size();
    if (winSize.width() <= 0 || winSize.height() <= 0) {
        winSize = size();
    }
    if (winSize.width() <= 0 || winSize.height() <= 0) {
        winSize = QSize(900, 640);
    }

    QPoint topLeft(globalClickPos.x() + 36, globalClickPos.y() - winSize.height() - 64);
    if (QScreen *screen = QGuiApplication::screenAt(globalClickPos)) {
        const QRect avail = screen->availableGeometry();
        if (topLeft.x() + winSize.width() > avail.right()) {
            topLeft.setX(avail.right() - winSize.width());
        }
        if (topLeft.y() < avail.top()) {
            topLeft.setY(avail.top());
        }
        if (topLeft.x() < avail.left()) {
            topLeft.setX(avail.left());
        }
    }
    showNormal();
    move(topLeft);
    QTimer::singleShot(0, this, [this, topLeft]() {
        move(topLeft);
        raise();
        activateWindow();
    });
    setWindowOpacity(1.0);
    m_trayHideAnimating = false;
    raise();
    activateWindow();
}

// 绘制窗口
void MainWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QPixmap background = m_appBackgroundPixmap.isNull() ? QPixmap(":/pictures/background.jpg") : m_appBackgroundPixmap;
    if (background.isNull()) {
        qWarning("背景图像加载失败");
        return;
    }
    QSize newSize = QSize(this->width(), this->height());
    QPixmap scaledBackground = background.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    int x = (this->width() - scaledBackground.width()) / 2;
    int y = (this->height() - scaledBackground.height()) / 2;
    int radius = 10;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(path);
    painter.drawPixmap(x, y, scaledBackground);
    QMainWindow::paintEvent(event);
}

// 鼠标按下
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    int frame = 5;
    QList<QWidget*> widgets = this->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        widget->clearFocus();
    }
    QPoint pos = event->pos();
    int margin = 30;
    if (event->button() == Qt::LeftButton) {
        if (pos.x() > width() - frame || pos.y() > height() - frame) {
            m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
            m_resizeFlag = 1;
            event->accept();
        }
        else if (pos.x() > width() - margin && pos.y() > height() - margin) {
            m_resizeFlag = 1;
            m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
            event->accept();
        }
        else if (pos.x() <= 30 || pos.x() >= width() - 30 ||
                 pos.y() <= 30 || pos.y() >= height() - 30){
            m_moveFlag = 1;
            m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
            event->accept();
        }
    }
    QMainWindow::mousePressEvent(event);
}

// 鼠标移动
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    int frame = 5;
    QPoint globalPos = event->globalPosition().toPoint();
    QPoint delta = globalPos - (this->frameGeometry().topLeft() + m_dragPosition);
    if (event->buttons() & Qt::LeftButton) {
        if (m_resizeFlag == 1) {
            QSize newSize = this->size();
            if (m_dragPosition.x() > width() - frame) {
                newSize.setWidth(this->width() + delta.x());
                setCursor(Qt::SizeHorCursor);
            }
            else if (m_dragPosition.y() > height() - frame) {
                newSize.setHeight(this->height() + delta.y());
                setCursor(Qt::SizeVerCursor);
            }
            if (m_dragPosition.x() > width() - frame && m_dragPosition.y() > height() - frame) {
                newSize.setWidth(this->width() + delta.x());
                newSize.setHeight(this->height() + delta.y());
                setCursor(Qt::SizeFDiagCursor);
            }
            QSize minSize(100, 100);
            newSize.setWidth(qMax(newSize.width(), minSize.width()));
            newSize.setHeight(qMax(newSize.height(), minSize.height()));
            this->resize(newSize);
            event->accept();
        }
        else if (m_moveFlag == 1) {
            this->move(globalPos - m_dragPosition);
            event->accept();
        }
        m_dragPosition = globalPos - this->frameGeometry().topLeft();
    }
    QMainWindow::mouseMoveEvent(event);
}

// 鼠标释放
void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    m_resizeFlag = 0;
    unsetCursor();
    QMainWindow::mouseReleaseEvent(event);
}

// 初始化头像
void MainWindow::initAvatar()
{
    QString appDir = ClientConfigDefaults::dataDir();
    QDir dir(appDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QPixmap pixmap = AvatarManager::getInstance()->loadAvator(m_myInfo.account, QSize(0, 0));
    if (pixmap.isNull()) {
        pixmap.load(":/pictures/suliao_avator_normal.jpg");
    }
    pixmap = pixmap.scaled(52, 52, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap roundedPixmap = AvatarManager::getRoundedPixmap(pixmap, 25);
    ui->lab_avator->setFixedSize(50, 50);
    ui->lab_avator->setPixmap(roundedPixmap);
    ui->lab_avator->setStyleSheet(
        "QLabel {"
        "    border: 3px solid rgb(80, 140, 255);"
        "    border-radius: 25px;"
        "    background-color: rgba(255, 204, 213, 0.2);"
        "    padding: 0px;"
        "}"
        );
    ui->lab_avator->setScaledContents(true);
}

// 初始化语音配置
void MainWindow::setupAudioSettings()
{
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
    m_audioBuffer = new QBuffer(this);
    m_audioTimer = new QTimer(this);
    m_audioTimer->setSingleShot(true);
    connect(m_audioTimer, &QTimer::timeout, this, &MainWindow::on_but_tool_sendaudio_released);
}

// 设置发送按钮位置
void MainWindow::positionSendButton() {}

// 创建用户目录
QString MainWindow::createUserDirs(const QString& account)
{
    const QString dataDir = ClientConfigDefaults::dataDir();
    QString dbDir = dataDir + QStringLiteral("database");
    m_apDir = dataDir + QStringLiteral("apdir") + QDir::separator() + account;
    m_avaDir = dataDir + QStringLiteral("avadir") + QDir::separator() + account;
    QDir dir;
    if (!dir.exists(dbDir)) {
        if (!dir.mkpath(dbDir)) { qDebug() << "创建数据库目录失败:" << dbDir; return ""; }
    }
    if (!dir.exists(m_apDir)) {
        if (!dir.mkpath(m_apDir)) { qDebug() << "创建数据库缓存目录失败:" << m_apDir; return ""; }
    }
    if (!dir.exists(m_avaDir)) {
        if (!dir.mkpath(m_avaDir)) { qDebug() << "创建头像缓存目录失败:" << m_apDir; return ""; }
    }
    QString dbName = dbDir + QDir::separator() + account + ".db";
    QString connectionName = "sqlite_connection_" + account;
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase oldDb = QSqlDatabase::database(connectionName);
        if (oldDb.isOpen()) oldDb.close();
        oldDb = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(dbName);
    if (!m_db.open()) { qDebug() << "打开数据库失败:" << m_db.lastError().text(); return ""; }
    QSqlQuery query(m_db);
    query.exec("CREATE TABLE IF NOT EXISTS messages ("
               "message_id BLOB PRIMARY KEY NOT NULL,"
               "sender VARCHAR(20), receiver VARCHAR(20),"
               "messagetype VARCHAR(20), message LONGTEXT,"
               "server_timestamp DATETIME NOT NULL,"
               "received_timestamp DATETIME NOT NULL)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_sender_receiver_ts "
               "ON messages(sender, receiver, received_timestamp)");
    query.exec("CREATE TABLE IF NOT EXISTS talks ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "friend_id VARCHAR(20), unread INT DEFAULT 0,"
               "latest_msg VARCHAR(200) DEFAULT '', timestamp DATETIME)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_talks_friend_id ON talks(friend_id)");
    return dbName;
}

// 删除用户数据库
void MainWindow::destroyUserDatabase(const QString& account)
{
    QString connectionName = "sqlite_connection_" + account;
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    const QString appDir = ClientConfigDefaults::dataDir();
    QString dbDir = appDir + QStringLiteral("database");
    QString dbName = dbDir + QDir::separator() + account + ".db";
    if (QFile::exists(dbName)) QFile::remove(dbName);
}

// 清理缓存
void MainWindow::clearAccountCache(const QString& account)
{
    if (account.isEmpty()) return;
    QDir targetApDir(m_apDir);
    if (targetApDir.exists()) targetApDir.removeRecursively();
    QDir targetAvaDir(m_avaDir);
    if (targetAvaDir.exists()) targetAvaDir.removeRecursively();
}

// 发送初始化请求
void MainWindow::havelogin(const QString& account)
{
    if(!SocketOnly::instance().isConnected()) return;
    QJsonObject jsonObj;
    jsonObj["tag"] = "askforloginmes0";
    jsonObj["account"] = account;
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 开始下载切换界面显示
void MainWindow::setDownLoad(const QString& path)
{
    setButtonProgress(ui->but_download, 0, "正在下载路径:\n" + path);
}

// 结束下载切换界面显示
void MainWindow::clearDownLoad()
{
    m_downloadTransferStartMs = 0;
    clearButtonProgress(ui->but_download, "无下载");
}

// 将字节数与耗时格式化为 MB/s 文本
QString MainWindow::formatTransferSpeedMbps(qint64 bytes, qint64 elapsedMs) const
{
    if (bytes <= 0 || elapsedMs < 200) return QString();
    const double sec = static_cast<double>(elapsedMs) / 1000.0;
    const double mbPerSec = (static_cast<double>(bytes) / (1024.0 * 1024.0)) / sec;
    return QStringLiteral("%1 MB/s").arg(mbPerSec, 0, 'f', 2);
}

// 更新网盘弹窗内下载进度
void MainWindow::notifyCloudDriveDownloadProgress(int percent)
{
    if (!m_cloudDriveDownloading || !m_cloudDrive) return;
    const qint64 elapsedMs = m_downloadTransferStartMs > 0
                                 ? QDateTime::currentMSecsSinceEpoch() - m_downloadTransferStartMs : 0;
    m_cloudDrive->setDownloadProgress(percent, formatTransferSpeedMbps(m_downloadingBytes, elapsedMs));
}

// 结束网盘弹窗内下载状态
void MainWindow::finishCloudDriveDownload(const QString &status)
{
    if (!m_cloudDriveDownloading || !m_cloudDrive) return;
    m_cloudDriveDownloading = false;
    m_cloudDrive->finishDownload(status);
}

// 开始上传切换界面显示
void MainWindow::setUpload(const QString& path)
{
    m_uploadTransferStartMs = QDateTime::currentMSecsSinceEpoch();
    m_lastUploadProgressPercent = 0;
    setButtonProgress(ui->but_upload, 0, "正在上传文件: " + path);
}

// 结束上传切换界面显示
void MainWindow::clearUpload()
{
    m_uploadTransferStartMs = 0;
    m_lastUploadProgressPercent = -1;
    clearButtonProgress(ui->but_upload, "无上传");
}

// 设置按钮进度
void MainWindow::setButtonProgress(QPushButton *btn, int percent, const QString &baseTip)
{
    if (!btn) return;
    percent = qBound(0, percent, 100);
    const QString tip = QString("(%1%) ").arg(percent) + baseTip;
    btn->setToolTip(tip);
    const double p = percent / 100.0;
    const QString overlay = QString(
        "QPushButton {"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 rgba(120, 200, 255, 0.45),"
        "stop:%1 rgba(120, 200, 255, 0.45),"
        "stop:%2 rgba(255, 245, 248, 1.0),"
        "stop:1 rgba(255, 245, 248, 1.0)"
        ");"
        "}"
    ).arg(QString::number(p, 'f', 3), QString::number(qMin(1.0, p + 0.001), 'f', 3));
    const QString base = (btn == ui->but_upload) ? m_uploadBtnBaseStyle : (btn == ui->but_download ? m_downloadBtnBaseStyle : btn->styleSheet());
    btn->setStyleSheet(base + "\n" + overlay);
}

// 清除按钮进度
void MainWindow::clearButtonProgress(QPushButton *btn, const QString &baseTip)
{
    if (!btn) return;
    btn->setToolTip(baseTip);
    const QString base = (btn == ui->but_upload) ? m_uploadBtnBaseStyle : (btn == ui->but_download ? m_downloadBtnBaseStyle : btn->styleSheet());
    btn->setStyleSheet(base);
}

// 播放音效
void MainWindow::playSound()
{
    if (m_soundFlag && m_sound)
        m_sound->play();
}

// 二进制转uuid
QString MainWindow::binaryUuidToString(const QByteArray& binaryUuid)
{
    if (binaryUuid.size() != 16) return "";
    QUuid uuid = QUuid::fromRfc4122(binaryUuid);
    if (uuid.isNull()) return "";
    return uuid.toString(QUuid::WithoutBraces);
}

// 处理某个待插入的消息记录
void MainWindow::pushMessageToDatabase(const QString& uuid)
{
    clearPendingOutboundAckWatch(uuid);
    if (!m_messageHash.contains(uuid)) return;
    const QString uk = messageUuidKey(uuid);
    MessageData msgData = m_messageHash.value(uuid);
    m_messageHash.remove(uuid);
    if (!uk.isEmpty() && m_recentMessageUuidKeys.contains(uk)) return;
    if (!uk.isEmpty()) m_recentMessageUuidKeys.insert(uk);
    m_msgQueue->enqueue(msgData);
}

// 为乐观发送的消息登记 messagehavedone（uuid）截止时刻，启动扫描定时器
void MainWindow::registerPendingOutboundAckWatch(const QString& uuid)
{
    if (uuid.isEmpty()) return;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch()
                            + ClientConfigDefaults::defaultOutboundMessageAckTimeoutMs();
    m_pendingMessageAckDeadlineMs.insert(uuid, deadline);
    if (m_pendingMessageAckSweepTimer && !m_pendingMessageAckDeadlineMs.isEmpty())
        m_pendingMessageAckSweepTimer->start();
}

// 收到服务端确认或放弃监视时移除对应截止记录
void MainWindow::clearPendingOutboundAckWatch(const QString& uuid)
{
    m_pendingMessageAckDeadlineMs.remove(uuid);
    if (m_pendingMessageAckDeadlineMs.isEmpty() && m_pendingMessageAckSweepTimer)
        m_pendingMessageAckSweepTimer->stop();
}

// 周期检查：超时仍未确认的从 m_messageHash 与界面收回
void MainWindow::sweepPendingOutboundAckTimeouts()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QString> overdue;
    for (auto it = m_pendingMessageAckDeadlineMs.constBegin(); it != m_pendingMessageAckDeadlineMs.constEnd(); ++it) {
        if (it.value() <= now && m_messageHash.contains(it.key())) overdue.append(it.key());
    }
    for (const QString& u : overdue) handlePendingOutboundAckTimeout(u);
    if (m_pendingMessageAckDeadlineMs.isEmpty() && m_pendingMessageAckSweepTimer)
        m_pendingMessageAckSweepTimer->stop();
}

// 单条处理：移除缓存、重载对端已打开会话页并 flush talks
void MainWindow::handlePendingOutboundAckTimeout(const QString& uuid)
{
    if (!m_messageHash.contains(uuid)) { clearPendingOutboundAckWatch(uuid); return; }
    const MessageData md = m_messageHash.value(uuid);
    const QString peer = (md.sender == m_myInfo.account) ? md.receiver : md.sender;
    m_messageHash.remove(uuid);
    const QString uk = messageUuidKey(uuid);
    if (!uk.isEmpty()) m_recentMessageUuidKeys.remove(uk);
    clearPendingOutboundAckWatch(uuid);
    if (!peer.isEmpty() && m_talksItem.contains(peer)) {
        reloadTalkPageFromDatabase(peer);
        flushTalksCacheToDatabase();
    }
    qWarning() << "消息未在时限内收到服务端确认 uuid=" << uuid;
}

// 清理头像缓存目录，每个账号仅保留最新的头像文件
bool MainWindow::cleanAvatarCache(const QString& avaDir)
{
    if (avaDir.isEmpty()) return false;
    QDir avatarDir(avaDir);
    if (!avatarDir.exists()) return true;
    QRegularExpression avatarRegex("^([^_]+)_(\\d+)\\.png$");
    QMap<QString, QMap<qint64, QString>> accountAvatarMap;
    QStringList allFiles = avatarDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    foreach (const QString& fileName, allFiles) {
        QRegularExpressionMatch match = avatarRegex.match(fileName);
        if (!match.hasMatch()) continue;
        QString account = match.captured(1);
        qint64 timestamp = match.captured(2).toLongLong();
        if (timestamp <= 0 || account.isEmpty()) continue;
        accountAvatarMap[account].insert(timestamp, fileName);
    }
    bool allCleanSuccess = true;
    QMapIterator<QString, QMap<qint64, QString>> accountIter(accountAvatarMap);
    while (accountIter.hasNext()) {
        accountIter.next();
        QMap<qint64, QString> avatarFiles = accountIter.value();
        if (avatarFiles.size() <= 1) continue;
        QList<qint64> timestamps = avatarFiles.keys();
        for (int i = 0; i < timestamps.size() - 1; ++i) {
            QString oldFilePath = avatarDir.filePath(avatarFiles.value(timestamps.at(i)));
            if (!QFile::remove(oldFilePath)) allCleanSuccess = false;
        }
    }
    return allCleanSuccess;
}

// 主连接不可用：禁用发送区与附件按钮，并中断进行中的下载
void MainWindow::applyNetworkOfflineUi()
{
    ui->edit_input->setEnabled(false);
    ui->but_tool_sendpic->setEnabled(false);
    ui->but_tool_sendfile->setEnabled(false);
    ui->but_tool_sendaudio->setEnabled(false);
    refreshSendButtonState();
}

// 主连接恢复：按当前选中会话恢复输入区与按钮可用性
void MainWindow::applyNetworkOnlineUi()
{
    onTalkItemCurrentChanged();
    refreshSendButtonState();
}

// 主连接恢复：关闭提示并重新走 askforloginmes0 同步会话
void MainWindow::onMainSocketConnected()
{
    if (m_reconnectTipDialog) {
        m_reconnectTipDialog->setUserDismissEnabled(true);
        m_reconnectTipDialog->close();
        m_reconnectTipDialog.clear();
    }
    if (m_didFirstHavelogin) {
        applyNetworkOnlineUi();
        if (!m_myInfo.account.isEmpty() && SocketOnly::instance().isConnected())
            m_loginResyncDebounce->start();
    }
}

// 已登录且尚无提示时弹出不可关闭的重连说明（与 socketDisconnected / socketError 共用）
void MainWindow::showReconnectTipDialogIfNeeded()
{
    if (!m_didFirstHavelogin || m_reconnectTipDialog) return;
    Dialog *d = new Dialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->transText(QStringLiteral("网络异常，正在尝试重连…"));
    d->setConfirmButtonVisible(false);
    d->setUserDismissEnabled(false);
    m_reconnectTipDialog = d;
    d->show();
}

// 主连接错误：不退出主窗口，交由 SocketOnly 退避重连
void MainWindow::onMainSocketError(const QString &errorString)
{
    Q_UNUSED(errorString);
    if (!m_didFirstHavelogin) return;
    applyNetworkOfflineUi();
    showReconnectTipDialogIfNeeded();
}

// 主连接重试达到上限：允许关闭提示并在关闭后退出客户端
void MainWindow::onMainSocketReconnectFailed(int retryCount)
{
    if (!m_didFirstHavelogin) return;
    applyNetworkOfflineUi();
    showReconnectTipDialogIfNeeded();
    if (!m_reconnectTipDialog) return;
    const int safeRetryCount = qMax(0, retryCount);
    m_reconnectTipDialog->transText(QStringLiteral("网络错误:重试%1次后连接失败").arg(safeRetryCount));
    m_reconnectTipDialog->setConfirmButtonVisible(true);
    m_reconnectTipDialog->setUserDismissEnabled(true);
    disconnect(m_reconnectTipDialog, nullptr, this, nullptr);
    connect(m_reconnectTipDialog, &QDialog::finished, this, [this](int) { quitApplicationFromUi(); });
}