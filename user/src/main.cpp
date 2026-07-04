/**
 * @file main.cpp
 * 客户端入口：默认配置、Socket 单例、登录后主窗口。
 */
#include "Login.h"
#include "MainWindow.h"
#include <QApplication>
#include <QPointer>
#include "SocketOnly.h"
#include "SocketDoc.h"
#include "ClientConfigDefaults.h"


// 程序入口
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/pictures/suliao_normal.png")));
    ClientConfigDefaults::ensureClientDefaults();
    SocketOnly::instance();
    SocketDocRead::instance();
    SocketDocWrite::instance();

    QPointer<Login> login = new Login;
    QPointer<MainWindow> mainWind = nullptr;
    QObject::connect(login, &Login::loginSucceedSig, [&](const QString &account) {
        if (!mainWind) {
            mainWind = new MainWindow(account);
            QObject::connect(mainWind, &MainWindow::loadFinishedSig, login, [&login, &mainWind]{
                if (login) {
                    login->close();
                    login->deleteLater();
                }
                if (mainWind) {
                    mainWind->show();
                    mainWind->setWindowIcon(QIcon(QStringLiteral(":/pictures/suliao_normal.png")));
                }
            });
        }
    });

    login->show();

    qAddPostRoutine([]() {
        SocketDocWrite::releaseInstance();
    });
    qAddPostRoutine([]() {
        SocketDocRead::releaseInstance();
    });
    qAddPostRoutine([]() {
        SocketOnly::releaseInstance();
    });

    int ret = a.exec();
    return ret;
}
