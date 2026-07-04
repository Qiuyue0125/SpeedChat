/**
 * @file MainWindowUI.cpp
 * 主窗口界面搭建：列表样式、菜单、主题与部分槽绑定。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ChoiceDialog.h"
#include "Dialog.h"
#include "ChangePassword.h"
#include "Logout.h"
#include "ClientConfigDefaults.h"
#include "MainWindowElse.h"
#ifdef HAS_WEBENGINE
#include <QWebEngineProfile>
#endif
#include <QVBoxLayout>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardItem>

// 获取配置文件路径（统一使用 ClientConfigDefaults 以支持 AppImage/Linux）
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

// 初始化好友列表
void MainWindow::setupFriendList()
{
    // 设置列表
    ui->list_friends->setViewMode(QListView::ListMode);// 设置视图模式为列表模式
    ui->list_friends->setSelectionBehavior(QAbstractItemView::SelectRows);// 设置选择行为为选择整行
    ui->list_friends->setSelectionMode(QAbstractItemView::SingleSelection);// 设置选择模式为单选
    ui->list_friends->setEditTriggers(QAbstractItemView::NoEditTriggers);// 禁止编辑
    ui->list_friends->setItemDelegate(new FriendDelegate(this));// 设置自定义的委托用于绘制列表项
    // 设置项
    m_friendModel = new QStandardItemModel(this);
    ui->list_friends->setModel(m_friendModel);
    QStandardItem *addFriendItem0 = new QStandardItem("新的朋友");
    QStandardItem *addFriendItem = new QStandardItem("新的朋友");
    QStandardItem *friendListItem = new QStandardItem("好友列表");
    // 设置不可选标志
    addFriendItem0->setFlags(Qt::NoItemFlags);
    addFriendItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    friendListItem->setFlags(Qt::NoItemFlags);
    // 将项添加到模型中
    m_friendModel->appendRow(addFriendItem0);
    m_friendModel->appendRow(addFriendItem);
    m_friendModel->appendRow(friendListItem);
    // 初始化模型
    m_filterProxyModel = new FilterProxyModel(this);
    m_filterProxyModel->setSourceModel(m_friendModel);
    // 设置模型到列表视图
    ui->list_friends->setModel(m_filterProxyModel);
    // 连接信号与槽
    connect(ui->list_friends->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onFriendItemCurrentChanged);
}

// 初始化聊天列表
void MainWindow::setupTalkList()
{
    // 设置列表
    ui->list_talks->setViewMode(QListView::ListMode);// 设置视图模式为列表模式
    ui->list_talks->setSelectionBehavior(QAbstractItemView::SelectRows);// 设置选择行为为选择整行
    ui->list_talks->setSelectionMode(QAbstractItemView::SingleSelection);// 设置选择模式为单选
    ui->list_talks->setEditTriggers(QAbstractItemView::NoEditTriggers);// 禁止编辑
    ui->list_talks->setItemDelegate(new TalkDelegate(this));// 设置自定义的委托用于绘制列表项

    // 设置标准模型
    m_talkModel = new QStandardItemModel(this);
    // 初始化过滤代理模型
    m_talkFilterProxyModel = new TalkFilterProxyModel(this);
    m_talkFilterProxyModel->setSourceModel(m_talkModel);
    // 设置模型到列表视图
    ui->list_talks->setModel(m_talkFilterProxyModel);
    // 连接信号与槽
    connect(ui->list_talks->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTalkItemCurrentChanged);
}

// 初始化菜单
void MainWindow::setupMenu()
{
    // 初始化
    QMenu* toolMenu = new QMenu(this);
    toolMenu->setStyleSheet(
        "QMenu {"
        "   background-color: rgb(240, 240, 240);"
        "   border-radius: 3px;"
        "   border: 0.5px solid rgb(0, 0, 0);"
        "}"
        "QMenu::item {"
        "   padding: 10px;"
        "   text-align: left;"
        "}"
        "QMenu::item:selected {"
        "   background-color: rgb(200, 200, 200);"
        "}"
        "QMenu::item:pressed {"
        "   background-color: rgb(180, 180, 180);"
        "}");
    // 添加菜单项

    QAction *actionBgApp = toolMenu->addAction("更换程序背景");
    QAction *actionBgChat = toolMenu->addAction("更换聊天背景");
    QAction *actionBgReset = toolMenu->addAction("恢复默认背景");
    QAction *actionAiProviders = toolMenu->addAction("AI助手管理");
    QAction *action0 = toolMenu->addAction("消息音效");
    action0->setCheckable(true);
    action0->setChecked(true);
    QAction *action1 = toolMenu->addAction("修改密码");
    QAction *action2 = toolMenu->addAction("注销账号");
    QAction *action3 = toolMenu->addAction("退出账号");
    QAction *action4 = toolMenu->addAction("清除缓存");
    connect(action0, &QAction::triggered, this, [this]() {
        m_soundFlag = !m_soundFlag;
    });
    connect(actionBgApp, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(nullptr, tr("选择程序背景图片"),
                                                          QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
        if (path.isEmpty()) return;
        const QString imported = importThemeImage(path, "app_background");
        applyAppBackground(imported.isEmpty() ? path : imported);
        saveThemeSetting(userThemeKey("AppBackground"), m_appBackgroundPath);
    });
    connect(actionBgChat, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(nullptr, tr("选择聊天背景图片"),
                                                          QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
        if (path.isEmpty()) return;
        const QString imported = importThemeImage(path, "chat_background");
        applyChatBackground(imported.isEmpty() ? path : imported);
        saveThemeSetting(userThemeKey("ChatBackground"), m_chatBackgroundPath);
    });
    connect(actionBgReset, &QAction::triggered, this, [this]() {
        applyAppBackground(QString());
        applyChatBackground(QString());
    });
    connect(actionAiProviders, &QAction::triggered, this, [this]() {
        openAiProvidersDialog();
    });
    connect(action1, &QAction::triggered, this, [this]() {
        if( m_changePassFlag == 0){
            m_dialogChangePass = new ChangePassword(this);
            connect(m_dialogChangePass,&ChangePassword::changePassword1,this,&MainWindow::sendMessageToServer);
            connect(m_dialogChangePass,&ChangePassword::changePassword2,this,&MainWindow::sendMessageToServer);
            connect(this,&MainWindow::changePasswordAnswer1,m_dialogChangePass,&::ChangePassword::changePasswordAns1);
            connect(this,&MainWindow::changePasswordAnswer2,m_dialogChangePass,&::ChangePassword::changePasswordAns2);
            connect(m_dialogChangePass, &ChangePassword::customClose, this, [this]() {
                qDebug()<<"修改密码窗口重置了";
                m_changePassFlag = 0;
                m_dialogChangePass->disconnect();
            });
            m_dialogChangePass->show();
            m_changePassFlag  = 1;
        }
        else if(m_changePassFlag  == 1){
            m_dialogChangePass->raise();
            if (m_dialogChangePass) {
                int x = this->x() + (this->width() - m_dialogChangePass->width()) / 2;
                int y = this->y() + (this->height() - m_dialogChangePass->height()) / 2;
                m_dialogChangePass->move(x, y);
            }
        }
    });
    connect(action2, &QAction::triggered, this, [this]() {
        if( m_logoutFlag == 0){
            m_logout = new Logout(this);
            connect(m_logout, &Logout::logout, this, &MainWindow::sendMessageToServer);
            connect(this, &MainWindow::logoutAnswer, m_logout, &Logout::logoutAnswer);
            connect(m_logout, &Logout::customClose, this, [this]() {
                qDebug()<<"注销窗口重置了";
                m_logoutFlag = 0;
                m_logout->disconnect();
            });
            m_logout->show();
            m_logoutFlag  = 1;
        }
        else if(m_logoutFlag  == 1){
            m_logout->raise();
            if (m_logout) {
                int x = this->x() + (this->width() - m_logout->width()) / 2;
                int y = this->y() + (this->height() - m_logout->height()) / 2;
                m_logout->move(x, y);
            }
        }
    });
    connect(action3, &QAction::triggered, this, [this]() {
        ChoiceDialog *dialog = new ChoiceDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->transText(QStringLiteral("确定要退出吗?"));
        if(!m_savePath.isEmpty() || !m_uploadFile["filename"].toString().isEmpty()){
            dialog->transText(QStringLiteral("有文件正在处理,确定要退出吗?"));
        }
        dialog->transButText(QStringLiteral("确认"), QStringLiteral("取消"));
        connect(dialog, &ChoiceDialog::accepted, this, [this]() {
            quitApplicationFromUi();
        });
        dialog->show();
    });
    connect(action4, &QAction::triggered, this, [this]() {
        if(cleanAvatarCache(m_avaDir)){
            Dialog* dialog = new Dialog(this);
            dialog->transText("清除成功!");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }else{
            Dialog* dialog = new Dialog(this);
            dialog->transText("清除失败!");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }
    });
    // 连接按钮的 clicked 信号
    connect(ui->but_set, &QPushButton::clicked, [=]() {
        toolMenu->adjustSize();

        QPoint btnGlobalTopLeft = ui->but_set->mapToGlobal(QPoint(0, 0));

        QPoint menuPos(
            btnGlobalTopLeft.x(),
            btnGlobalTopLeft.y() - toolMenu->height()
            );

        toolMenu->popup(menuPos, nullptr);
    });
}

// 设置云盘页面
void MainWindow::setupCloudPage()
{
    // 将 CloudDrive 控件嵌入 page_cloud，放在标题栏下方
    m_cloudDrive = new CloudDrive(ui->page_cloud);

    // 连接 CloudDrive 信号
    connect(m_cloudDrive, &CloudDrive::cloudUploadRequested, this, &MainWindow::startCloudUpload);
    connect(m_cloudDrive, &CloudDrive::cloudSearchRequested, this, &MainWindow::searchCloudFile);
    connect(m_cloudDrive, &CloudDrive::cloudDownloadRequested, this, &MainWindow::downloadCloudFile);
    connect(m_cloudDrive, &CloudDrive::cloudListMyFilesRequested, this, &MainWindow::listMyCloudFiles);

    // 添加到 page_cloud 布局的第二行（第一行是标题栏按钮）
    if (QGridLayout *cloudLayout = qobject_cast<QGridLayout*>(ui->page_cloud->layout())) {
        cloudLayout->addWidget(m_cloudDrive, 1, 0, 1, 4);
    }
}

// 设置AI页面
void MainWindow::setupAiPage()
{
#ifdef HAS_WEBENGINE
    // 检查账号是否有效
    if (m_myInfo.account.isEmpty()) {
        qWarning() << "Account number is empty, using default profile";
        m_myInfo.account = "default";
    }

    // 创建账号特定的存储路径
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + "/profiles/account_" + m_myInfo.account;
    QDir().mkpath(dataPath);

    // 创建账号特定的Profile名称
    QString profileName = "Profile_Account_" + m_myInfo.account;

    // 创建自定义Profile，指定名称和存储路径
    QWebEngineProfile *persistentProfile = new QWebEngineProfile(profileName, this);
    persistentProfile->setPersistentStoragePath(dataPath);
    // 允许持久化存储Cookie、本地存储等
    persistentProfile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

    // 设置HTTP缓存
    persistentProfile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    persistentProfile->setCachePath(dataPath + "/cache");

    // 设置下载路径
    persistentProfile->setDownloadPath(dataPath + "/downloads");
    QDir().mkpath(dataPath + "/downloads");

    // 使用这个Profile创建Web视图
    m_webView = new QWebEngineView(persistentProfile, ui->frame_ai);
    // 初始URL不写死（从配置的AI列表/选中项读取）
    loadAiProviders();
    if (!m_aiProviders.isEmpty() && m_selectedAiIndex >= 0 && m_selectedAiIndex < m_aiProviders.size()) {
        m_webView->setUrl(QUrl(m_aiProviders[m_selectedAiIndex].url));
    } else {
        m_webView->setUrl(QUrl("about:blank"));
    }

    // 设置布局
    QVBoxLayout *layout = new QVBoxLayout(ui->frame_ai);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    loadAiProviders();
    rebuildAiProviderButtons();
#else
    Q_UNUSED(ui);
    // 无 WebEngine：添加提示标签
    QLabel *tip = new QLabel(ui->frame_ai);
    tip->setText(QStringLiteral("AI 内嵌浏览器不可用\n\n编译时未找到 Qt WebEngineWidgets。\n请安装 qt6-webengine-dev 后重新编译以获得此功能。"));
    tip->setAlignment(Qt::AlignCenter);
    tip->setStyleSheet("QLabel { color: #7f8c8d; font: 13pt 'Microsoft YaHei UI'; }");
    QVBoxLayout *layout = new QVBoxLayout(ui->frame_ai);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addWidget(tip);
    loadAiProviders();
    rebuildAiProviderButtons();
#endif
}

// 加载AI助手配置
void MainWindow::loadAiProviders()
{
    ClientConfigDefaults::ensureClientDefaults();
    QSettings settings(settingsIniPath(), QSettings::IniFormat);

    auto parseProviders = [&](const QString &jsonText) -> QVector<AiProvider> {
        QVector<AiProvider> out;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
        if (!doc.isArray()) return out;
        const QJsonArray arr = doc.array();
        out.reserve(arr.size());
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            const QString name = o.value("name").toString().trimmed();
            const QString url = o.value("url").toString().trimmed();
            if (name.isEmpty() || url.isEmpty()) continue;
            out.push_back({name, url});
        }
        return out;
    };

    // 优先读取当前账号的AI配置（多账号隔离）
    QVector<AiProvider> providers = parseProviders(settings.value(userAiKey("Providers")).toString());
    if (providers.isEmpty()) {
        // 其次读取全局默认（来自 clientconfigdefaults）
        providers = parseProviders(settings.value("AI/Providers").toString());
    }
    m_aiProviders = providers;

    // 读取当前账号的选中索引（多账号隔离），没有则用全局默认
    m_selectedAiIndex = settings.value(userAiKey("SelectedIndex"),
                                       settings.value("AI/SelectedIndex",
                                                      ClientConfigDefaults::defaultAiSelectedIndex())).toInt();
    if (m_selectedAiIndex < 0 || m_selectedAiIndex >= m_aiProviders.size()) {
        m_selectedAiIndex = 0;
    }
}

// 保存AI助手配置
void MainWindow::saveAiProviders()
{
    QJsonArray arr;
    for (const auto &p : m_aiProviders) {
        if (p.name.trimmed().isEmpty() || p.url.trimmed().isEmpty()) continue;
        QJsonObject o;
        o["name"] = p.name.trimmed();
        o["url"] = p.url.trimmed();
        arr.append(o);
    }

    QSettings settings(settingsIniPath(), QSettings::IniFormat);
    if (arr.isEmpty()) {
        settings.remove(userAiKey("Providers"));
        settings.remove(userAiKey("SelectedIndex"));
    } else {
        settings.setValue(userAiKey("Providers"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        settings.setValue(userAiKey("SelectedIndex"), m_selectedAiIndex);
    }
    settings.sync();
}

// 重新构建AI助手页面
void MainWindow::rebuildAiProviderButtons()
{
    // 清掉旧按钮
    for (auto *b : m_aiProviderButtons) {
        if (!b) continue;
        ui->horizontalLayout_3->removeWidget(b);
        b->deleteLater();
    }
    m_aiProviderButtons.clear();

    if (m_aiProviders.isEmpty()) {
        loadAiProviders();
    }
    if (m_aiProviders.isEmpty()) return;

    // 插入位置：在 spacer( ui->spa ) 之前
    int insertAt = ui->horizontalLayout_3->indexOf(ui->spa);
    if (insertAt < 0) insertAt = ui->horizontalLayout_3->count();

    for (int i = 0; i < m_aiProviders.size(); ++i) {
        const auto &p = m_aiProviders[i];
        auto *btn = new QPushButton(ui->page_ai);
        btn->setText(p.name);
        btn->setMinimumSize(QSize(90, 30));
        btn->setMaximumSize(QSize(90, 30));
        btn->setCursor(QCursor(Qt::PointingHandCursor));
        btn->setStyleSheet(i == m_selectedAiIndex ? BTN_SELECTED_STYLE : BTN_UNSELECTED_STYLE);
        ui->horizontalLayout_3->insertWidget(insertAt + i, btn);
        m_aiProviderButtons.push_back(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i < 0 || i >= m_aiProviders.size()) return;
            m_selectedAiIndex = i;
            for (int j = 0; j < m_aiProviderButtons.size(); ++j) {
                if (!m_aiProviderButtons[j]) continue;
                m_aiProviderButtons[j]->setStyleSheet(j == m_selectedAiIndex ? BTN_SELECTED_STYLE : BTN_UNSELECTED_STYLE);
            }
#ifdef HAS_WEBENGINE
            if (m_webView) {
                m_webView->setUrl(QUrl(m_aiProviders[i].url));
            }
#endif
            QSettings settings(settingsIniPath(), QSettings::IniFormat);
            settings.setValue(userAiKey("SelectedIndex"), m_selectedAiIndex);
            settings.sync();
        });
    }

#ifdef HAS_WEBENGINE
    // 初次进来直接跳转到选中项
    if (m_webView && m_selectedAiIndex >= 0 && m_selectedAiIndex < m_aiProviders.size()) {
        m_webView->setUrl(QUrl(m_aiProviders[m_selectedAiIndex].url));
    }
#endif
}

// 打开AI提供商管理对话框
void MainWindow::openAiProvidersDialog()
{
    loadAiProviders();

    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("AI助手管理");
    dlg->setWindowModality(Qt::NonModal);
    dlg->setMinimumSize(600, 400);

    QString globalStyle = R"(
        /* 彻底重置对话框样式 */
        QDialog {
            background-color: #f8f9fa;
            border: none !important;
            outline: none !important;
        }

        /* 按钮样式 */
        QPushButton {
            padding: 6px 16px;
            border-radius: 4px;
            border: 1px solid #ced4da !important;
            background-color: #ffffff !important;
            font-size: 13px;
            outline: none !important;
        }
        QPushButton:hover {
            background-color: #e9ecef !important;
            border-color: #adb5bd !important;
        }
        QPushButton:pressed {
            background-color: #dee2e6 !important;
        }

        /* 表格整体样式 */
        QTableWidget {
            gridline-color: #e9ecef !important;
            border: 1px solid #ced4da !important;
            border-radius: 4px !important;
            background-color: #ffffff !important;
            outline: none !important;
            selection-background-color: #e9ecef !important;
            selection-color: #000000 !important;
        }

        /* 表头样式 - 彻底移除粉色边框 */
        QHeaderView {
            border: none !important;
            outline: none !important;
        }
        QHeaderView::section {
            background-color: #e9ecef !important;
            border: none !important;
            border-right: 1px solid #ced4da !important; /* 仅保留列分隔线 */
            padding: 8px !important;
            font-weight: 500 !important;
            outline: none !important;
            selection-background-color: #e9ecef !important;
        }
        QHeaderView::section:selected {
            background-color: #e9ecef !important;
            border: none !important;
        }

        /* 表格单元格样式 */
        QTableWidget::item {
            border: none !important;
            outline: none !important;
        }
        QTableWidget::item:selected {
            background-color: #e9ecef !important;
            color: #000000 !important;
            border: none !important;
        }

        /* 表格编辑框样式 */
        QTableWidget QLineEdit {
            border: 1px solid #adb5bd !important;
            border-radius: 3px !important;
            padding: 2px !important;
            outline: none !important;
        }

        /* 按钮盒样式 */
        QDialogButtonBox {
            border: none !important;
            outline: none !important;
        }
    )";

    dlg->setStyleSheet(globalStyle);

    auto *table = new QTableWidget(dlg);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(QStringList() << "名称" << "URL");

    // 表头样式优化
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setMinimumHeight(36);
    table->horizontalHeader()->setStyleSheet(R"(
        QHeaderView::section {
            background-color: #e9ecef !important;
            border: none !important;
            outline: none !important;
        }
    )");
    table->verticalHeader()->setVisible(false);

    // 表格交互设置
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(globalStyle + R"(
        QTableWidget {
            alternate-background-color: #f8f9fa !important;
        }
    )");

    auto fill = [&]() {
        table->setRowCount(m_aiProviders.size());
        for (int r = 0; r < m_aiProviders.size(); ++r) {
            QTableWidgetItem *nameItem = new QTableWidgetItem(m_aiProviders[r].name);
            QTableWidgetItem *urlItem = new QTableWidgetItem(m_aiProviders[r].url);
            nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            urlItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            table->setItem(r, 0, nameItem);
            table->setItem(r, 1, urlItem);
        }
        table->resizeColumnToContents(0);
        table->setColumnWidth(0, qMax(table->columnWidth(0), 150));
    };
    fill();

    // 按钮创建
    auto *btnAdd = new QPushButton("新增", dlg);
    auto *btnDel = new QPushButton("删除", dlg);
    btnDel->setCursor(Qt::PointingHandCursor);
    btnAdd->setCursor(Qt::PointingHandCursor);

    btnDel->setEnabled(false);

    auto *btnSave = new QPushButton(QIcon(":/pictures/icon_save.png"), "保存", dlg);
    btnSave->setCursor(Qt::PointingHandCursor);

    // 选择变化信号
    connect(table, &QTableWidget::currentItemChanged, this, [=](QTableWidgetItem*, QTableWidgetItem*) {
        btnDel->setEnabled(table->currentRow() >= 0);
    });

    connect(btnAdd, &QPushButton::clicked, dlg, [table]() {
        const int r = table->rowCount();
        table->insertRow(r);

        QTableWidgetItem *newNameItem = new QTableWidgetItem("新助手");
        QTableWidgetItem *newUrlItem = new QTableWidgetItem("https:// ");
        newNameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        newUrlItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        table->setItem(r, 0, newNameItem);
        table->setItem(r, 1, newUrlItem);
        table->setCurrentCell(r, 0);
        table->editItem(table->item(r, 0));
        table->resizeColumnToContents(0);
        table->setColumnWidth(0, qMax(table->columnWidth(0), 150));
    });

    connect(btnDel, &QPushButton::clicked, dlg, [this, dlg, table, btnDel]() {
        const int r = table->currentRow();
        if (r < 0) return;

        ChoiceDialog *dialog = new ChoiceDialog(dlg);
        dialog->transText("你确定要删除该链接吗?");
        dialog->transButText("确认", "取消");
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        connect(dialog, &ChoiceDialog::accepted, this, [this, r, table, btnDel]() {
            if (!table || !btnDel) return;
            if (r < 0 || r >= table->rowCount()) return;

            table->removeRow(r);
            if (table->rowCount() > 0) {
                table->setCurrentCell(qMin(r, table->rowCount()-1), 0);
            } else {
                btnDel->setEnabled(false);
            }
        });

        dialog->open();
    });

    connect(btnSave, &QPushButton::clicked, dlg, [this, dlg, table]() {
        QVector<AiProvider> newList;
        for (int r = 0; r < table->rowCount(); ++r) {
            const QString name = table->item(r, 0) ? table->item(r, 0)->text().trimmed() : QString();
            const QString url = table->item(r, 1) ? table->item(r, 1)->text().trimmed() : QString();
            if (name.isEmpty() || url.isEmpty()) continue;
            newList.push_back({name, url});
        }
        if (newList.isEmpty()) {
            Dialog* dialog = new Dialog(this);
            dialog->transText("请至少保留一个链接!");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            return;
        }
        m_aiProviders = newList;
        if (m_selectedAiIndex < 0 || m_selectedAiIndex >= m_aiProviders.size()) {
            m_selectedAiIndex = 0;
        }
        saveAiProviders();
        rebuildAiProviderButtons();
        dlg->close();
    });

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(btnAdd);
    buttonLayout->addSpacing(10);
    buttonLayout->addWidget(btnDel);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(btnSave);
    buttonLayout->setContentsMargins(0, 0, 0, 15);

    auto *mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(table);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    dlg->resize(720, 450);
    // 将对话框移动到主窗口中心，而不是屏幕中心
    QRect mainGeo = this->geometry();
    QPoint mainCenter = mainGeo.center();
    QSize dlgSize = dlg->size();
    QPoint targetTopLeft(mainCenter.x() - dlgSize.width() / 2,
                         mainCenter.y() - dlgSize.height() / 2);
    dlg->move(targetTopLeft);
    dlg->show();
}

// 获取用户AI配置键名
QString MainWindow::userAiKey(const QString &suffix) const
{
    return "AI/" + safeKeyPart(m_myInfo.account) + "/" + suffix;
}

// 加载主题设置
void MainWindow::loadThemeSettings()
{
    QSettings settings(settingsIniPath(), QSettings::IniFormat);
    m_appBackgroundPath = settings.value(userThemeKey("AppBackground"), "").toString();
    m_chatBackgroundPath = settings.value(userThemeKey("ChatBackground"), "").toString();

    applyAppBackground(m_appBackgroundPath);
    applyChatBackground(m_chatBackgroundPath);
}

// 导入主题图片
QString MainWindow::importThemeImage(const QString &srcPath, const QString &destBaseName)
{
    if (srcPath.isEmpty() || destBaseName.isEmpty()) return QString();
    if (!QFile::exists(srcPath)) return QString();

    QDir dir(themeDirPath(m_myInfo.account));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    const QString suffix = QFileInfo(srcPath).suffix().toLower();
    const QString ext = suffix.isEmpty() ? "png" : suffix;
    const QString destPath = dir.filePath(destBaseName + "." + ext);

    // 如果用户选的就是我们的内部文件，直接使用，避免"删自己再拷贝自己"的问题
    const QString srcAbs = QFileInfo(srcPath).absoluteFilePath();
    const QString destAbs = QFileInfo(destPath).absoluteFilePath();
    if (QDir::cleanPath(srcAbs) == QDir::cleanPath(destAbs)) {
        return destAbs;
    }

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }
    if (!QFile::copy(srcPath, destPath)) {
        return QString();
    }
    return destPath;
}

// 获取用户主题配置键名
QString MainWindow::userThemeKey(const QString &suffix) const
{
    return "Theme/" + safeKeyPart(m_myInfo.account) + "/" + suffix;
}

// 保存主题设置
void MainWindow::saveThemeSetting(const QString &key, const QString &value)
{
    QSettings settings(settingsIniPath(), QSettings::IniFormat);
    if (value.isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key, value);
    }
    settings.sync();
}

// 应用程序背景
void MainWindow::applyAppBackground(const QString &pathOrEmpty)
{
    m_appBackgroundPath = pathOrEmpty;
    m_appBackgroundPixmap = QPixmap();

    if (!m_appBackgroundPath.isEmpty() && QFile::exists(m_appBackgroundPath)) {
        // 迁移/导入：仅当不是theme目录内部文件时才导入
        if (!QDir::cleanPath(QFileInfo(m_appBackgroundPath).absoluteFilePath()).startsWith(QDir::cleanPath(themeDirPath(m_myInfo.account)))) {
            const QString imported = importThemeImage(m_appBackgroundPath, "app_background");
            if (!imported.isEmpty()) {
                m_appBackgroundPath = imported;
            }
        }
        QPixmap pix(m_appBackgroundPath);
        if (!pix.isNull()) {
            m_appBackgroundPixmap = pix;
        } else {
            m_appBackgroundPath.clear();
        }
    }

    // 保存配置（空路径时自动移除配置项）
    saveThemeSetting(userThemeKey("AppBackground"), m_appBackgroundPath);
    update();
}

// 应用聊天背景
void MainWindow::applyChatBackground(const QString &pathOrEmpty)
{
    m_chatBackgroundPath = pathOrEmpty;

    if (!m_chatBackgroundPath.isEmpty() && QFile::exists(m_chatBackgroundPath)) {
        if (!QDir::cleanPath(QFileInfo(m_chatBackgroundPath).absoluteFilePath()).startsWith(QDir::cleanPath(themeDirPath(m_myInfo.account)))) {
            const QString imported = importThemeImage(m_chatBackgroundPath, "chat_background");
            if (!imported.isEmpty()) {
                m_chatBackgroundPath = imported;
            }
        }
    }

    // 重置默认背景
    if (m_chatBackgroundPath.isEmpty() || !QFile::exists(m_chatBackgroundPath)) {
        ui->stack_talks->setDefaultBackgroundPath(":/pictures/tafei.png");
        ui->edit_show2->setDefaultBackgroundPath(":/pictures/tafei.png");
        ui->stack_talks->setDefaultBack();
        ui->edit_show2->setDefaultBack();
        m_chatBackgroundPath.clear();
    } else {
        QPixmap pix(m_chatBackgroundPath);
        if (pix.isNull()) {
            ui->stack_talks->setDefaultBackgroundPath(":/pictures/tafei.png");
            ui->edit_show2->setDefaultBackgroundPath(":/pictures/tafei.png");
            ui->stack_talks->setDefaultBack();
            ui->edit_show2->setDefaultBack();
            m_chatBackgroundPath.clear();
        } else {
            ui->stack_talks->setDefaultBackgroundPath(m_chatBackgroundPath);
            ui->edit_show2->setDefaultBackgroundPath(m_chatBackgroundPath);
            ui->stack_talks->setBackground(pix);
            ui->edit_show2->setBackground(pix);
        }
    }

    // 保存配置（空路径时自动移除配置项）
    saveThemeSetting(userThemeKey("ChatBackground"), m_chatBackgroundPath);
}
