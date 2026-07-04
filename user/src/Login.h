#ifndef LOGIN_H
#define LOGIN_H

/**
 * @file Login.h
 * 登录与自动登录入口。
 */

#include <QMainWindow>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QByteArray>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QFocusEvent>
#include <QSettings>
#include <QStyle>
#include <QPixmap>
#include <QPainter>
#include <QBitmap>
#include <QIcon>
#include <QBrush>
#include <QTimer>
#include <QLinearGradient>
#include <QStandardPaths>
#include "RegisterWindow.h"
#include "FindPassword.h"
#include "SocketDepend.h"
#include "ClientConfigDefaults.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class Login;
}
QT_END_NAMESPACE


class Login : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数
    Login(QWidget *parent = nullptr);
    // 析构函数
    ~Login();

protected:
    // 绘制渐变背景
    void paintEvent(QPaintEvent *event);
    // 拖动窗口
    void mouseMoveEvent(QMouseEvent *event);
    // 结束拖动
    void mouseReleaseEvent(QMouseEvent *event);
    // 点击窗口处理
    void mousePressEvent(QMouseEvent *event);

private:
    // 初始化头像
    void setAva();
    // 初始化图标
    void setIcon();
    // 设置输入校验
    void setVal();
    // 设置计时器
    void setTimer();
    // 设置输入框样式
    void setSty();
    // DPAPI：对“记住密码”做不可逆加密（Windows only）
    QString dpapiEncrypt(const QString &input);
    // 解密（跨平台，纯 Qt 实现）
    QString dpapiDecrypt(const QString &input);
    // 检查自动登录
    void ifAutoLogin();
    // 登录成功处理
    void loginSucceed();
    // 登录失败处理
    void loginFail();
    // 设置控件可用状态
    void setWidgetsEnable(bool enable);
    // 设置登录按钮状态
    void setLoginButtonLoading(bool loading);

private slots:
    // 更新渐变背景
    void updateGradient();
    // 更新按钮文本
    void updateLoginText();
    // 发送数据到服务器
    void sendMessageToServer(const QJsonObject& ject);
    // 读取服务器数据
    void onReadyRead(const QByteArray& data);
    // 发送登录请求
    void sendLogin();
    // 点击登录
    void on_gologin_clicked();
    // 密码框回车触发登录
    void on_line_password_returnPressed();
    // 账号框回车触发登录
    void on_line_num_returnPressed();
    // 账号输入变化
    void on_line_num_textChanged(const QString &arg1);
    // 密码输入变化
    void on_line_password_textChanged(const QString &arg1);
    // 自动登录勾选
    void on_ckbox_auto_toggled(bool checked);
    // 记住密码勾选
    void on_ckbox_remeber_toggled(bool checked);
    // 打开注册窗口
    void on_rgs_pbt_clicked();
    // 打开找回密码窗口
    void on_fgt_pbt_clicked();
    // 关闭窗口
    void on_but_deletewindow_clicked();
    // 三角按钮：弹出本机登录记录菜单
    void on_tool_login_history_clicked();

private:
    // 读取本机登录账号记录（JSON 数组）
    QJsonArray loadLoginHistoryArray() const;
    // 保存本机登录账号记录（JSON 数组）
    void saveLoginHistoryArray(const QJsonArray &arr);
    // 登录成功后写入/刷新登录历史
    void recordSuccessfulLoginInHistory();
    // 将历史账号填回登录表单
    void applyHistoryAccountToForm(const QString &account);
    // 弹出登录历史菜单
    void showLoginHistoryPopup();
    // 删除某条登录历史账号记录
    void removeHistoryAccount(const QString &account);
    // 登录页头像：有账号则尝试本地缓存头像，否则用默认图
    void applyLoginWindowAvatar(const QString &accountOrEmpty);
    // 按当前输入框账号刷新登录页头像
    void applyLoginWindowAvatarFromForm();

signals:
    // 注册失败信号
    void regisFail();
    // 注册成功信号
    void regisSucceed(const QString &account);
    // 找回密码步骤一信号
    void findPass1(const QJsonObject &jsonObj);
    // 找回密码步骤二信号
    void findPass2(const QJsonObject &jsonObj);
    // 找回密码步骤三信号
    void findPass3(const QJsonObject &jsonObj);
    // 登录成功信号
    void loginSucceedSig(const QString &account);

private:
    Ui::Login *ui;
    // 接收状态
    RecvState m_recvState = RecvState::WaitHeader;
    // 待接收数据长度
    uint32_t m_expectedDataLen = 0;
    QByteArray m_recvBuffer;
    QRegularExpressionValidator *validator;
    QSettings m_settings = QSettings(ClientConfigDefaults::settingsIniPath(),
                                     QSettings::IniFormat);
    // 登录配置
    QPoint m_dragPosition;
    QTimer *m_cancelTimer;
    // 按钮文本计时器
    QTimer *m_textUpdateTimer;
    // 文本点计数
    int m_dotCount = 0;
    // 是否拖动窗口
    bool m_moveFlag = 0;
    bool m_loginSucceedFlag = false;
    // 渐变偏移
    int m_offset = 0;
    // 注册窗口对象
    RegisterWindow* m_regis;
    // 找回密码窗口对象
    FindPassword* m_findPassword;
    static const QString BTN_ENABLE_STYLE;
    static const QString BTN_DISABLE_STYLE;
    static const QString BTN_LOADING_STYLE;
    static constexpr int kLoginHistoryMax = 12;
};


// 输入框类
class LineInput :public QLineEdit
{
    Q_OBJECT

public:
    LineInput(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    // 获得焦点处理
    void focusInEvent(QFocusEvent *event);
    // 失去焦点处理
    void focusOutEvent(QFocusEvent *event);
};

#endif // LOGIN_H
