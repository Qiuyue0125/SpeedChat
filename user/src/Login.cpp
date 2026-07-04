/**
 * @file Login.cpp
 * 登录/注册入口界面与账号流程。
 */
#include "Login.h"
#include "ui_Login.h"
#include "Dialog.h"
#include "SocketOnly.h"
#include "AvatarManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QJsonArray>
#include <QJsonValue>
#include <QMenu>
#include <QRegularExpression>
#include <QPushButton>
#include <QWidgetAction>
#include <QLabel>
#include <cmath>

#include <QCryptographicHash>
#include <QSysInfo>

#include "CloseButtonUtils.h"

namespace {

// 登录记录下拉框统一样式（透明底，圆角由内容容器负责）。
QString loginHistoryMenuGlobalStyle()
{
    return QStringLiteral(
        "QMenu {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}");
}

// 从本地 avadir/<account>/ 读取该账号最新头像并做圆角处理。
QPixmap pixmapFromAccountAvatarDir(const QString &account, int actualSize, int radius)
{
    if (account.isEmpty())
        return QPixmap();
    // 与 MainWindow::cleanAvatarCache 一致：本机目录在 avadir/<账号>/，且仅「账号_时间戳」文件才是该用户自己的头像；
    // 同目录下还有好友头像文件，不能按修改时间任取一张。
    const QString dirPath = QDir(QDir(ClientConfigDefaults::dataDir()).filePath(QStringLiteral("avadir")))
                               .filePath(account);
    QDir dir(dirPath);
    if (!dir.exists())
        return QPixmap();

    const QRegularExpression re(
        QStringLiteral("^%1_(\\d+)\\.(png|jpg|jpeg)$").arg(QRegularExpression::escape(account)),
        QRegularExpression::CaseInsensitiveOption);

    qint64 bestTs = -1;
    QString bestPath;
    const QFileInfoList files = dir.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
        QDir::Files,
        QDir::NoSort);
    for (const QFileInfo &fi : files) {
        const QRegularExpressionMatch m = re.match(fi.fileName());
        if (!m.hasMatch())
            continue;
        const qint64 ts = m.capturedView(1).toLongLong();
        if (ts > bestTs) {
            bestTs = ts;
            bestPath = fi.absoluteFilePath();
        }
    }
    if (bestPath.isEmpty())
        return QPixmap();

    QPixmap p;
    if (!p.load(bestPath) || p.isNull())
        return QPixmap();
    p = p.scaled(actualSize, actualSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return AvatarManager::getRoundedPixmap(p, radius);
}

// 计算登录记录菜单弹出位置：与账号输入框左侧对齐并贴底显示。
QPoint loginHistoryMenuExecPos(const QWidget *anchor, const QSize &menuSize)
{
    const QPoint bl = anchor->mapToGlobal(QPoint(0, anchor->height()));
    int x = bl.x();
    if (QScreen *scr = anchor->screen()) {
        const QRect avail = scr->availableGeometry();
        if (x < avail.left())
            x = avail.left();
        if (x + menuSize.width() > avail.right())
            x = avail.right() - menuSize.width();
    }
    return QPoint(x, bl.y());
}

} // namespace

const QString Login::BTN_ENABLE_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(5, 186, 251);
        color: white;
        border-radius: 15px;
    }
    QPushButton:hover {
        background-color: rgba(5, 186, 251, 0.7);
        color: white;
        border-radius: 15px;
    }
    QPushButton:pressed {
        background-color: rgba(0, 123, 255, 0.8);
        color: rgba(255, 255, 255, 0.9);
        border-radius: 15px;
    }
)";

const QString Login::BTN_DISABLE_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(167, 214, 255);
        color: white;
        border-radius: 15px;
    }
    QPushButton:hover {
        background-color: rgba(5, 186, 251, 0.7);
        color: white;
        border-radius: 15px;
    }
    QPushButton:pressed {
        background-color: rgba(0, 123, 255, 0.8);
        color: rgba(255, 255, 255, 0.9);
        border-radius: 15px;
    }
)";

const QString Login::BTN_LOADING_STYLE = R"(
    QPushButton {
        font: 12pt 'Microsoft YaHei UI';
        background-color: rgb(255, 0, 0);
        color: white;
        border-radius: 15px;
    }
    QPushButton:hover {
        background-color: rgba(255, 0, 0, 0.7);
        color: white;
        border-radius: 15px;
    }
    QPushButton:pressed {
        background-color: rgba(200, 0, 0, 0.8);
        color: rgba(255, 255, 255, 0.9);
        border-radius: 15px;
    }
)";

// 构造函数
Login::Login(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Login)
{
    ui->setupUi(this);
    // 初始化窗口
    setWindowIcon(QIcon(":/pictures/suliao.png"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    // 关闭按钮统一样式
    CloseButtonUtils::setup(ui->but_deletewindow, QIcon(":/pictures/icon_close_hover.png"));
    // 初始化头像
    setAva();
    // 初始化图标
    setIcon();
    // 设置输入校验
    setVal();
    // 设置计时器
    setTimer();
    // 连接信号
    connect(&SocketOnly::instance(), &SocketOnly::dataReceived,
            this, &Login::onReadyRead, Qt::QueuedConnection);
    applyLoginWindowAvatarFromForm();
    this->show();
    ifAutoLogin();
    applyLoginWindowAvatarFromForm();
}

// 析构函数
Login::~Login()
{
    qDebug()<<"登录窗口关闭";
    delete ui;
}

// 绘制渐变背景：主色仍为左上粉桃(255,g1,235)与右下天蓝(172,224,249)，仅增加色标与反向叠层
void Login::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const int g1 = 220 + (std::sin(m_offset * 0.06) * 30);
    const QColor warm(255, g1, 235);
    const QColor cool(172, 224, 249);

    const qreal w = width();
    const qreal h = height();

    auto lerpRgb = [](const QColor &a, const QColor &b, qreal t) {
        t = qBound(0.0, t, 1.0);
        return QColor(
            int(a.red() + (b.red() - a.red()) * t + 0.5),
            int(a.green() + (b.green() - a.green()) * t + 0.5),
            int(a.blue() + (b.blue() - a.blue()) * t + 0.5));
    };

    const QColor onChord = lerpRgb(warm, cool, 0.5);
    const int breathe = int(4 * std::sin(m_offset * 0.048));
    const QColor lift(qMin(255, onChord.red() + 14 + breathe),
                      qMin(255, onChord.green() + 12 + breathe),
                      qMin(255, onChord.blue() + 10 + breathe));

    QLinearGradient mainGrad(0, 0, w, h);
    mainGrad.setColorAt(0.0, warm);
    mainGrad.setColorAt(0.30, lerpRgb(warm, lift, 0.48));
    mainGrad.setColorAt(0.50, lift);
    mainGrad.setColorAt(0.74, lerpRgb(lift, cool, 0.62));
    mainGrad.setColorAt(1.0, cool);
    painter.setBrush(mainGrad);
    painter.drawRect(rect());

    const double phase = std::sin(m_offset * 0.042);
    const QColor washA = lerpRgb(warm, cool, 0.28 + 0.07 * phase);
    const QColor washB = lerpRgb(warm, cool, 0.76 - 0.05 * phase);
    QLinearGradient cross(w, 0, 0, h);
    cross.setColorAt(0.0, QColor(washA.red(), washA.green(), washA.blue(), 50));
    cross.setColorAt(0.48, QColor(lift.red(), lift.green(), lift.blue(), 22));
    cross.setColorAt(1.0, QColor(washB.red(), washB.green(), washB.blue(), 44));
    painter.setBrush(cross);
    painter.drawRect(rect());

    painter.setPen(QPen(Qt::black));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, 0, 0));

    QMainWindow::paintEvent(event);
}

// 拖动窗口
void Login::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton&&this->rect().contains(event->pos()) && m_moveFlag ==1) {
        this->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
    QMainWindow::mouseMoveEvent(event);
}

// 结束拖动
void Login::mouseReleaseEvent(QMouseEvent *event)
{
    m_moveFlag = 0;
    QMainWindow::mouseReleaseEvent(event);
}

// 点击窗口处理
void Login::mousePressEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    ui->line_password->clearFocus();
    ui->line_num->clearFocus();
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - this->geometry().topLeft();
        if (pos.x() <= 30 || pos.x() >= width() - 30 ||
            pos.y() <= 30 || pos.y() >= height() - 30){
            qDebug() << "点击在边缘";
            m_moveFlag = 1;
        }
        event->accept();
    }
    QMainWindow::mousePressEvent(event);
}

// 初始化头像
void Login::setAva()
{
    ui->lab_avator->setStyleSheet(
        "QLabel {"
        "    border: 3px solid #3498db;"
        "    border-radius: 15px;"
        "    background-color: white;"
        "}"
        );

    ui->lab_avator->setFixedSize(100, 100);
}

// 初始化图标
void Login::setIcon()
{
    QPixmap pixmap_tubiao(":/pictures/suliao.png");
    ui->lab_tubiao->setPixmap(pixmap_tubiao);
    ui->lab_tubiao->setScaledContents(true);
}

// 设置输入校验
void Login::setVal()
{
    QRegularExpression regExp("^[a-zA-Z0-9]{1,12}$");
    Login::validator = new QRegularExpressionValidator(regExp, this);
    ui->line_num->setValidator(validator);
    ui->line_password->setValidator(validator);
}

// 设置计时器
void Login::setTimer()
{
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Login::updateGradient);
    timer->start(50);
    m_cancelTimer = new QTimer(this);
    connect(m_cancelTimer, &QTimer::timeout, this, &Login::sendLogin);
    m_textUpdateTimer = new QTimer(this);
    connect(m_textUpdateTimer, &QTimer::timeout, this, &Login::updateLoginText);
}

// 设置输入框样式
void Login::setSty()
{
    if(ui->line_num->text() != "请输入你的账号"){
        ui->line_num->setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                                    "border: 1px solid rgba(0, 0, 0, 0.3); "
                                    "border-radius: 10px; "
                                    "padding-left: 17px; padding-right: 17px; "
                                    "color: black;");}
    if(ui->line_password->text() != "请输入你的密码"){
        ui->line_password->setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                                         "border: 1px solid rgba(0, 0, 0, 0.3); "
                                         "border-radius: 10px; "
                                         "padding-left: 26px; padding-right: 26px; "
                                         "color: black;");
        ui->line_password->setEchoMode(QLineEdit::Password);}
}

// 跨平台密码加密（纯 Qt 实现，SHA-256 + XOR，不依赖操作系统 API）
QString Login::dpapiEncrypt(const QString &input)
{
    static constexpr const char *kPrefix = "enc:";
    if (input.isEmpty())
        return QString();

    // 密钥 = 本机唯一ID + hostname → SHA-256
    const QByteArray seed = QSysInfo::machineUniqueId()
                            + QSysInfo::machineHostName().toUtf8();
    const QByteArray key = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);

    const QByteArray plain = input.toUtf8();
    QByteArray result = plain;
    for (int i = 0; i < result.size(); ++i) {
        result[i] = result[i] ^ key[i % key.size()]
                    ^ static_cast<char>((i * 37 + 13) & 0xFF);
    }

    return QString::fromLatin1(kPrefix) + result.toBase64();
}

// 跨平台密码解密
QString Login::dpapiDecrypt(const QString &input)
{
    static constexpr const char *kPrefix = "enc:";
    if (input.isEmpty())
        return QString();
    if (!input.startsWith(QString::fromLatin1(kPrefix)))
        return QString();

    const QString b64 = input.mid(QString::fromLatin1(kPrefix).size());
    const QByteArray encBytes = QByteArray::fromBase64(b64.toLatin1());
    if (encBytes.isEmpty())
        return QString();

    const QByteArray seed = QSysInfo::machineUniqueId()
                            + QSysInfo::machineHostName().toUtf8();
    const QByteArray key = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);

    QByteArray result = encBytes;
    for (int i = 0; i < result.size(); ++i) {
        result[i] = result[i] ^ key[i % key.size()]
                    ^ static_cast<char>((i * 37 + 13) & 0xFF);
    }

    return QString::fromUtf8(result);
}

// 检查自动登录
void Login::ifAutoLogin()
{
    QString account = m_settings.value("Login/lastlogin", "").toString();
    if(!account.isEmpty()){
        ui->line_num->setText(account);
    }

    QString encryptedPassword = m_settings.value("Login/password", "").toString();
    bool hasDecryptedPassword = false;
    if(!encryptedPassword.isEmpty()){
        // 解密密码
        const QString password = dpapiDecrypt(encryptedPassword);
        if (!password.isEmpty()) {
            ui->line_password->setText(password);
            ui->ckbox_remeber->setChecked(true);
            hasDecryptedPassword = true;
        } else {
            ui->line_password->setText(QStringLiteral("请输入你的密码"));
            ui->ckbox_remeber->setChecked(false);
        }
    }

    bool autoLogin = m_settings.value("Login/autologin", false).toBool();
    if (autoLogin && hasDecryptedPassword) {
        ui->ckbox_auto->setChecked(true);
        on_gologin_clicked();
    }
    else {
        ui->ckbox_auto->setChecked(false);
        if (autoLogin && !hasDecryptedPassword) {
            // 避免自动登录时把占位符当成密码发送
            m_settings.setValue("Login/autologin", false);
        }
        qDebug()<<"没有自动登录";
    }

    setSty();
}

// 登录成功处理
void Login::loginSucceed()
{
    qDebug() << "登录成功";
    ui->gologin->setEnabled(false);
    m_loginSucceedFlag = true;
    m_settings.setValue("Login/lastlogin", ui->line_num->text());//记录登录账号
    if (ui->ckbox_remeber->isChecked() && ui->ckbox_auto->isChecked()) {
        //加密密码后保存
        const QString encryptedPassword = dpapiEncrypt(ui->line_password->text());
        m_settings.setValue("Login/password", encryptedPassword);
        m_settings.setValue("Login/autologin", true);
    } else if (ui->ckbox_remeber->isChecked()) {
        //加密密码后保存
        const QString encryptedPassword = dpapiEncrypt(ui->line_password->text());
        m_settings.setValue("Login/autologin", false);
        m_settings.setValue("Login/password", encryptedPassword);
    } else {
        m_settings.setValue("Login/autologin", false);
        m_settings.setValue("Login/password", "");
    }
    // 清理旧键（避免再出现 [General] 残留）
    m_settings.remove("lastlogin");
    m_settings.remove("password");
    m_settings.remove("autologin");
    recordSuccessfulLoginInHistory();
    emit loginSucceedSig(ui->line_num->text());
}

// 登录失败处理
void Login::loginFail()
{
    setWidgetsEnable(true);
    setLoginButtonLoading(false);

    if (QTcpSocket* sock = SocketOnly::instance().socket(); sock && sock->state() == QAbstractSocket::ConnectedState) {
        QString text =  "请检查您的账号密码";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}

// 设置控件可用状态
void Login::setWidgetsEnable(bool enable)
{
    ui->ckbox_remeber->setEnabled(enable);
    ui->ckbox_auto->setEnabled(enable);
    ui->line_num->setEnabled(enable);
    ui->line_password->setEnabled(enable);
    ui->tool_login_history->setEnabled(enable);
    ui->rgs_pbt->setEnabled(enable);
    ui->fgt_pbt->setEnabled(enable);

    if (enable) {
        //恢复登录按钮状态
        on_line_num_textChanged(ui->line_num->text());
    }
}

// 设置登录按钮状态
void Login::setLoginButtonLoading(bool loading)
{
    if (loading) {
        ui->gologin->setEnabled(false);
        ui->gologin->setStyleSheet(BTN_LOADING_STYLE);
        ui->gologin->setText("正在登录.");
        m_dotCount = 1;
        m_textUpdateTimer->start(350);
    } else {
        m_textUpdateTimer->stop();
        ui->gologin->setText("安全登录");
        //恢复按钮状态
        on_line_num_textChanged(ui->line_num->text());
    }
}

// 更新渐变背景
void Login::updateGradient()
{
    m_offset = (m_offset + 1) % 62832;
    update();
}

// 更新按钮文本
void Login::updateLoginText()
{
    QString text = "正在登录.";
    if (m_dotCount < 6) {
        text.append(QString(".").repeated(m_dotCount));
        m_dotCount++;
    } else {
        m_dotCount = 1;//重置点计数
    }
    ui->gologin->setText(text);
}

// 发送数据到服务器
void Login::sendMessageToServer(const QJsonObject& ject)
{
    if (!SocketOnly::instance().isConnected()) {
        SocketOnly::instance().initializeConnection();
    }
    if(!SocketOnly::instance().isConnected()){
        QString text =  "请检查您的网络连接";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    QJsonDocument jsonDoc(ject);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    SocketOnly::instance().sendData(jsonData);
}

// 读取服务器数据
void Login::onReadyRead(const QByteArray& data)
{
    if(m_loginSucceedFlag) return;

    //将新收到的数据追加到缓冲区
    m_recvBuffer += data;

    while (!m_recvBuffer.isEmpty()) {
        switch (m_recvState) {
        case RecvState::WaitHeader: {
            const int HeaderLen = PROTOCOL_HEADER_LEN;

            //头部未接收完成，退出循环等待下次数据
            if (m_recvBuffer.length() < HeaderLen) {
                return;
            }

            //调用封装函数解析8字节协议头
            ProtocolHeader header = bytesToHeader(m_recvBuffer.left(HeaderLen));

            if (header.version != PROTOCOL_WIRE_VERSION) {
                qWarning() << "客户端收到不支持的协议版本：" << static_cast<int>(header.version);
                m_recvBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                return;
            }

            //从协议头中获取数据体长度
            m_expectedDataLen = header.dataLen;

            if (m_expectedDataLen == 0 || m_expectedDataLen > MAX_PACKET_SIZE) {
                qWarning() << "客户端收到非法数据长度：" << m_expectedDataLen;
                m_recvBuffer.clear();
                m_recvState = RecvState::WaitHeader;
                return;
            }

            m_recvBuffer = m_recvBuffer.mid(PROTOCOL_HEADER_LEN); //新逻辑：移除8字节
            m_recvState = RecvState::WaitData;
            break;
        }

        case RecvState::WaitData: {
            //数据体未接收完成，退出循环等待下次数据
            if (m_recvBuffer.length() < m_expectedDataLen) {
                return;
            }

            //提取完整数据体
            QByteArray completeData = m_recvBuffer.left(m_expectedDataLen);
            //移除已处理的部分
            m_recvBuffer = m_recvBuffer.mid(m_expectedDataLen);

            QJsonDocument jsonDoc = QJsonDocument::fromJson(completeData);
            if (jsonDoc.isNull()) {
                qDebug() << "未能解析 JSON 数据，当前完整包数据是:" << completeData;
                m_recvState = RecvState::WaitHeader;
                m_expectedDataLen = 0;
                continue; //跳过错误包，继续处理下一个
            }

            //处理解析成功的 JSON 对象
            QJsonObject jsonObj = jsonDoc.object();
            if (jsonObj["answer"] == "loginsucceed") {//登录成功
                loginSucceed();
            }
            else if (jsonObj["answer"] == "loginfaill") {//登录失败
                qDebug() << "登录失败";
                loginFail();
            }
            else if (jsonObj["answer"] == "regissucceed") {
                qDebug() << "注册成功了";
                emit regisSucceed(jsonObj["account_number"].toString());
            }
            else if (jsonObj["answer"] == "regisfail") {
                qDebug() << "注册失败了";
                emit regisFail();
            }
            else if (jsonObj["tag"] == "findpassword1_answer") {
                qDebug() << "收到找回密码有没有这个账号了";
                emit findPass1(jsonObj);
            }
            else if (jsonObj["tag"] == "findpassword2_answer") {
                qDebug() << "收到回发的密保问题结果了";
                emit findPass2(jsonObj);
            }
            else if (jsonObj["tag"] == "findpassword3_answer") {
                qDebug() << "收到修改密码结果了";
                emit findPass3(jsonObj);
            }

            m_recvState = RecvState::WaitHeader;
            m_expectedDataLen = 0;
            break;
        }
        }
    }
}

// 发送登录请求
void Login::sendLogin()
{
    m_cancelTimer->stop();
    setLoginButtonLoading(true);
    setWidgetsEnable(false);

    QJsonObject jsonObj;
    jsonObj["tag"] = "login";
    jsonObj["account_number"] = ui->line_num->text();
    jsonObj["password"] = ui->line_password->text();
    QJsonDocument jsondoc(jsonObj);
    QByteArray jsonData = jsondoc.toJson(QJsonDocument::Compact);
    if (!SocketOnly::instance().isConnected()) {
        SocketOnly::instance().initializeConnection();
    }
    if (!SocketOnly::instance().isConnected()) {
        QString text =  "请检查您的网络连接";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        //恢复控件状态
        setWidgetsEnable(true);
        setLoginButtonLoading(false);
        return;
    }
    SocketOnly::instance().sendData(jsonData);
    return;
}

// 点击登录
void Login::on_gologin_clicked()
{
    if (m_cancelTimer->isActive()) {
        m_cancelTimer->stop();
        //如果请求取消，重置状态并返回
        setWidgetsEnable(true);
        setLoginButtonLoading(false);
        return;
    }

    //设置加载状态
    setLoginButtonLoading(true);
    setWidgetsEnable(false);
    ui->gologin->setEnabled(true);

    //启动定时器，n秒后发送登录请求
    m_cancelTimer->start(1500);
}

// 密码框回车触发登录
void Login::on_line_password_returnPressed()
{
    on_gologin_clicked();
}

// 账号框回车触发登录
void Login::on_line_num_returnPressed()
{
    on_gologin_clicked();
}

// 账号输入变化
void Login::on_line_num_textChanged(const QString &arg1)
{
    if(!arg1.isEmpty() && arg1 != "请输入你的账号" && !ui->line_password->text().isEmpty() && ui->line_password->text() != "请输入你的密码"){
        ui->gologin->setEnabled(true);
        ui->gologin->setStyleSheet(BTN_ENABLE_STYLE);
    }
    else{
        ui->gologin->setEnabled(false);
        ui->gologin->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 密码输入变化
void Login::on_line_password_textChanged(const QString &arg1)
{
    if(!arg1.isEmpty() && arg1 != "请输入你的密码" && !ui->line_num->text().isEmpty() && ui->line_num->text() != "请输入你的账号"){
        ui->gologin->setEnabled(true);
        ui->gologin->setStyleSheet(BTN_ENABLE_STYLE);

    }
    else{
        ui->gologin->setEnabled(false);
        ui->gologin->setStyleSheet(BTN_DISABLE_STYLE);
    }
}

// 自动登录勾选
void Login::on_ckbox_auto_toggled(bool checked)
{
    if(ui->ckbox_auto->isChecked())
        ui->ckbox_remeber->setChecked(checked);
}

// 记住密码勾选
void Login::on_ckbox_remeber_toggled(bool checked)
{
    if (!checked) {
        ui->ckbox_auto->setChecked(false);
    }
}

// 打开注册窗口
void Login::on_rgs_pbt_clicked()
{
    if(ui->gologin->text() != "安全登录"){
        on_gologin_clicked();
    }
    if (!SocketOnly::instance().isConnected()) {
        SocketOnly::instance().initializeConnection();
    }
    if (!SocketOnly::instance().isConnected()) {
        QString text =  "请检查您的网络连接";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    m_regis = new RegisterWindow(this);
    connect(m_regis,&RegisterWindow::sendRegis, this,&Login::sendMessageToServer);
    connect(this,&Login::regisSucceed,m_regis,&RegisterWindow::regisSucceed);
    connect(this,&Login::regisFail,m_regis,&RegisterWindow::regisFail);
    m_regis->setAttribute(Qt::WA_DeleteOnClose);
    m_regis->show();
}

// 打开找回密码窗口
void Login::on_fgt_pbt_clicked()
{
    if(ui->gologin->text() != "安全登录"){
        on_gologin_clicked();
    }
    if (!SocketOnly::instance().isConnected()) {
        SocketOnly::instance().initializeConnection();
    }
    if (!SocketOnly::instance().isConnected()) {
        QString text =  "请检查您的网络连接";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    m_findPassword = new FindPassword(this);
    connect(m_findPassword,&FindPassword::sendForget, this,&Login::sendMessageToServer);
    connect(this,&Login::findPass1,m_findPassword,&FindPassword::findPassword1);
    connect(this,&Login::findPass2,m_findPassword,&FindPassword::findPassword2);
    connect(this,&Login::findPass3,m_findPassword,&FindPassword::findPassword3);
    m_findPassword->setAttribute(Qt::WA_DeleteOnClose);
    m_findPassword->show();
}

// 关闭窗口
void Login::on_but_deletewindow_clicked()
{
    this->close();
}

// 三角按钮：弹出本机登录记录菜单
void Login::on_tool_login_history_clicked()
{
    showLoginHistoryPopup();
}

// 读取本机登录账号记录（JSON 数组）。
QJsonArray Login::loadLoginHistoryArray() const
{
    const QString raw = m_settings.value(QStringLiteral("Login/historyAccounts")).toString();
    if (raw.isEmpty())
        return QJsonArray();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    return doc.isArray() ? doc.array() : QJsonArray();
}

// 保存本机登录账号记录（JSON 数组）。
void Login::saveLoginHistoryArray(const QJsonArray &arr)
{
    m_settings.setValue(QStringLiteral("Login/historyAccounts"),
                        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// 登录成功后写入/刷新历史记录（置顶当前账号并更新记住密码状态）。
void Login::recordSuccessfulLoginInHistory()
{
    const QString account = ui->line_num->text();
    if (account.isEmpty() || account == QStringLiteral("请输入你的账号"))
        return;

    QString pEnc;
    if (ui->ckbox_remeber->isChecked()) {
        const QString pw = ui->line_password->text();
        if (!pw.isEmpty() && pw != QStringLiteral("请输入你的密码"))
            pEnc = dpapiEncrypt(pw);
    }

    QJsonArray arr = loadLoginHistoryArray();
    QJsonArray newArr;
    QJsonObject head;
    head.insert(QStringLiteral("a"), account);
    head.insert(QStringLiteral("p"), pEnc);
    newArr.append(head);
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QString a = v.toObject().value(QStringLiteral("a")).toString();
        if (a == account)
            continue;
        newArr.append(v.toObject());
        if (newArr.size() >= kLoginHistoryMax)
            break;
    }
    saveLoginHistoryArray(newArr);
}

// 将选中的历史账号回填到表单，并同步密码与头像显示。
void Login::applyHistoryAccountToForm(const QString &account)
{
    if (account.isEmpty())
        return;

    ui->line_num->setText(account);
    ui->line_num->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                               "border: 1px solid rgba(0, 0, 0, 0.3); "
                                               "border-radius: 10px; "
                                               "padding-left: 17px; padding-right: 17px; "
                                               "color: black;"));

    QString pEnc;
    for (const QJsonValue &v : loadLoginHistoryArray()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("a")).toString() == account) {
            pEnc = o.value(QStringLiteral("p")).toString();
            break;
        }
    }

    if (!pEnc.isEmpty()) {
        const QString plain = dpapiDecrypt(pEnc);
        if (!plain.isEmpty()) {
            ui->line_password->setText(plain);
            ui->line_password->setEchoMode(QLineEdit::Password);
            ui->line_password->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                                            "border: 1px solid rgba(0, 0, 0, 0.3); "
                                                            "border-radius: 10px; "
                                                            "padding-left: 26px; padding-right: 26px; "
                                                            "color: black;"));
            ui->ckbox_remeber->setChecked(true);
        } else {
            ui->line_password->setText(QStringLiteral("请输入你的密码"));
            ui->line_password->setEchoMode(QLineEdit::Normal);
            ui->line_password->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                                            "border: 1px solid rgba(0, 0, 0, 0.3); "
                                                            "border-radius: 10px; "
                                                            "padding-left: 26px; padding-right: 26px; "
                                                            "color: grey;"));
            ui->ckbox_remeber->setChecked(false);
            ui->ckbox_auto->setChecked(false);
        }
    } else {
        ui->line_password->setText(QStringLiteral("请输入你的密码"));
        ui->line_password->setEchoMode(QLineEdit::Normal);
        ui->line_password->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                                        "border: 1px solid rgba(0, 0, 0, 0.3); "
                                                        "border-radius: 10px; "
                                                        "padding-left: 26px; padding-right: 26px; "
                                                        "color: grey;"));
        ui->ckbox_remeber->setChecked(false);
        ui->ckbox_auto->setChecked(false);
    }
    on_line_num_textChanged(account);
    applyLoginWindowAvatar(account);
}

// 弹出登录历史下拉菜单（支持选择账号与删除记录）。
void Login::showLoginHistoryPopup()
{
    QMenu menu(this);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu.setWindowFlag(Qt::FramelessWindowHint, true);
    menu.setStyleSheet(loginHistoryMenuGlobalStyle());

    // 外层白色圆角容器，提供菜单背景
    auto *container = new QWidget(&menu);
    container->setAutoFillBackground(false);
    container->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgb(255, 255, 255);"
        "  border: 1px solid rgba(0, 0, 0, 0.12);"
        "  border-radius: 10px;"
        "}"));
    auto *containerLay = new QVBoxLayout(container);
    containerLay->setContentsMargins(4, 4, 4, 4);
    containerLay->setSpacing(1);

    const QJsonArray arr = loadLoginHistoryArray();
    if (arr.isEmpty()) {
        auto *emptyLab = new QLabel(QStringLiteral("无保存记录"), container);
        emptyLab->setAlignment(Qt::AlignCenter);
        emptyLab->setStyleSheet(QStringLiteral(
            "color: #7f8c8d; font: 11pt 'Microsoft YaHei UI'; background: transparent;"
            " border: none; padding: 4px 0px;"));
        containerLay->addWidget(emptyLab);
    } else {
        for (const QJsonValue &v : arr) {
            if (!v.isObject())
                continue;
            const QString account = v.toObject().value(QStringLiteral("a")).toString();
            if (account.isEmpty())
                continue;

            auto *row = new QWidget(container);
            row->setAttribute(Qt::WA_Hover, true);
            row->setAutoFillBackground(false);
            row->setStyleSheet(QStringLiteral(
                "QWidget { background: transparent; border: none; border-radius: 8px; }"
                "QWidget:hover { background: rgba(5, 186, 251, 0.16); border: none; }"));
            auto *h = new QGridLayout(row);
            h->setContentsMargins(8, 4, 8, 4);
            h->setHorizontalSpacing(6);
            h->setVerticalSpacing(0);

            auto *pickBtn = new QPushButton(account, row);
            pickBtn->setFlat(true);
            pickBtn->setCursor(Qt::PointingHandCursor);
            pickBtn->setStyleSheet(QStringLiteral(
                "QPushButton { text-align: center; font: 11pt 'Microsoft YaHei UI'; color: #2c3e50; "
                "padding: 6px 0px; border: none; border-radius: 8px; background: transparent; }"
                "QPushButton:hover { background: transparent; color: #1a5276; }"));

            auto *leftPad = new QWidget(row);
            leftPad->setFixedSize(20, 1);
            leftPad->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

            auto *delBtn = new QPushButton(QStringLiteral("×"), row);
            delBtn->setFixedSize(28, 28);
            delBtn->setFlat(true);
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setStyleSheet(QStringLiteral(
                "QPushButton { font: 15pt 'Microsoft YaHei UI'; color: #e74c3c; border: none; "
                "border-radius: 8px; padding: 0; background: transparent; }"
                "QPushButton:hover { background: rgba(231, 76, 60, 0.85); color: rgb(230,230,230); }"));

            h->addWidget(leftPad, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            h->addWidget(pickBtn, 0, 1);
            h->addWidget(delBtn, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
            h->setColumnStretch(1, 1);

            containerLay->addWidget(row);

            connect(pickBtn, &QPushButton::clicked, [this, account, &menu]() {
                applyHistoryAccountToForm(account);
                menu.close();
            });
            connect(delBtn, &QPushButton::clicked, [this, account, &menu]() {
                removeHistoryAccount(account);
                menu.close();
            });
        }
    }

    container->adjustSize();
    const int targetWidth = ui->line_num->width();
    if (targetWidth > 0)
        container->setMinimumWidth(targetWidth);

    auto *wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(container);
    menu.addAction(wa);

    menu.adjustSize();
    menu.exec(loginHistoryMenuExecPos(ui->line_num, menu.sizeHint()));
}

// 删除指定历史账号，并在必要时重置当前输入框与头像。
void Login::removeHistoryAccount(const QString &account)
{
    if (account.isEmpty())
        return;

    QJsonArray newArr;
    for (const QJsonValue &v : loadLoginHistoryArray()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("a")).toString() == account)
            continue;
        newArr.append(o);
    }
    saveLoginHistoryArray(newArr);

    if (m_settings.value(QStringLiteral("Login/lastlogin")).toString() == account) {
        m_settings.remove(QStringLiteral("Login/lastlogin"));
        m_settings.remove(QStringLiteral("Login/password"));
        m_settings.remove(QStringLiteral("Login/autologin"));
        m_settings.sync();
    }

    if (ui->line_num->text() == account) {
        ui->line_num->setText(QStringLiteral("请输入你的账号"));
        ui->line_num->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                                   "border: 1px solid rgba(0, 0, 0, 0.3); "
                                                   "border-radius: 10px; "
                                                   "padding-left: 17px; padding-right: 17px; "
                                                   "color: grey;"));
        ui->line_password->setText(QStringLiteral("请输入你的密码"));
        ui->line_password->setEchoMode(QLineEdit::Normal);
        ui->line_password->setStyleSheet(QStringLiteral("font: 12pt 'Microsoft YaHei UI'; "
                                                        "border: 1px solid rgba(0, 0, 0, 0.3); "
                                                        "border-radius: 10px; "
                                                        "padding-left: 26px; padding-right: 26px; "
                                                        "color: grey;"));
        ui->ckbox_remeber->setChecked(false);
        ui->ckbox_auto->setChecked(false);
        applyLoginWindowAvatar(QString());
    }
    on_line_num_textChanged(ui->line_num->text());
}

// 根据账号（或空）刷新登录页头像。
void Login::applyLoginWindowAvatar(const QString &accountOrEmpty)
{
    const int actualSize = 100 - 6;
    const int radius = 11;
    QPixmap pixmap;

    auto loadDefaultRounded = [actualSize, radius]() {
        QPixmap def(QStringLiteral(":/pictures/suliao_avator_normal.jpg"));
        def = def.scaled(actualSize, actualSize,
                         Qt::KeepAspectRatioByExpanding,
                         Qt::SmoothTransformation);
        return AvatarManager::getRoundedPixmap(def, radius);
    };

    if (!accountOrEmpty.isEmpty()) {
        pixmap = pixmapFromAccountAvatarDir(accountOrEmpty, actualSize, radius);
        if (pixmap.isNull())
            pixmap = loadDefaultRounded();
    } else {
        pixmap = loadDefaultRounded();
    }

    ui->lab_avator->setPixmap(pixmap);
    ui->lab_avator->setAlignment(Qt::AlignCenter);
}

// 按当前输入框账号刷新登录页头像。
void Login::applyLoginWindowAvatarFromForm()
{
    QString acc = ui->line_num->text();
    if (acc.isEmpty() || acc == QStringLiteral("请输入你的账号"))
        acc.clear();
    applyLoginWindowAvatar(acc);
}

// 获得焦点处理
void LineInput::focusInEvent(QFocusEvent *event)
{
    if (objectName() == QStringLiteral("line_num"))
        setClearButtonEnabled(false);
    else if (objectName() == QStringLiteral("line_password"))
        setClearButtonEnabled(true);
    if(text() == "请输入你的密码"||text() == "请输入你的账号"){
        setText("");
        if (objectName() == QStringLiteral("line_num")) {
            setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                          "border: 1px solid rgba(0, 0, 0, 0.3); "
                          "border-radius: 10px; "
                          "padding-left: 17px; padding-right: 17px; "
                          "color: black;");
        } else {
            setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                          "border: 1px solid rgba(0, 0, 0, 0.3); "
                          "border-radius: 10px; "
                          "padding-left: 26px; padding-right: 26px; "
                          "color: black;");
        }
    }
    if(this->objectName() == "line_password")
        this->setEchoMode(QLineEdit::Password);
    QLineEdit::focusInEvent(event);
}

// 失去焦点处理事件
void LineInput::focusOutEvent(QFocusEvent *event)
{
    if (objectName() == QStringLiteral("line_num") || objectName() == QStringLiteral("line_password"))
        setClearButtonEnabled(false);
    if (objectName() == "line_password") {
        if (text().isEmpty()) {
            setText("请输入你的密码");
            setEchoMode(QLineEdit::Normal);
            setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                          "border: 1px solid rgba(0, 0, 0, 0.3); "
                          "border-radius: 10px; "
                          "padding-left: 26px; padding-right: 26px; "
                          "color: grey;");
        }
    } else if (objectName() == "line_num") {
        if (text().isEmpty()) {
            setText("请输入你的账号");
            setStyleSheet("font: 12pt 'Microsoft YaHei UI'; "
                          "border: 1px solid rgba(0, 0, 0, 0.3); "
                          "border-radius: 10px; "
                          "padding-left: 17px; padding-right: 17px; "
                          "color: grey;");
        }
    }
    QLineEdit::focusOutEvent(event);
}