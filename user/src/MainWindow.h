#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/**
 * @file MainWindow.h
 * 主窗口：会话列表、收发消息、Socket 与本地库协调。
 */

#include <QDebug>
#include <QListWidget>
#include <QMainWindow>
#include <QLabel>
#include <QTcpSocket>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QPainter>
#include <QPixmap>
#include <QBitmap>
#include <QPainterPath>
#include <QStandardItemModel>
#include <QBrush>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QFileDialog>
#include <QBuffer>
#include <QMouseEvent>
#include <QTimer>
#include <QMutex>
#include <QJsonArray>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QMenu>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QListView>
#include <QSoundEffect>
#include <QPushButton>
#include <QSqlError>
#include <QAudioSource>
#include <QAudioSink>
#ifdef HAS_WEBENGINE
#include <QWebEngineView>
#endif
#include <QVector>
#include <QSet>
#include <QPointer>
#include <QSystemTrayIcon>
#include "DbWorker.h"
#include "Dialog.h"
#include "AddFriends.h"
#include "ChangeInformation.h"
#include "ChangePassword.h"
#include "Logout.h"
#include "CloudDrive.h"
#include "MainWindowElse.h"
#include "SocketDepend.h"
#include "AccountMessageManager.h"

class FilterProxyModel;

class TalkFilterProxyModel;

struct AccountInfo;
class QAction;

namespace Ui {
class MainWindow;
}


// 主窗口
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 构造函数
    explicit MainWindow(QString accountNumber,QWidget *parent = nullptr);
    // 析构函数
    ~MainWindow();

protected:
    // 关闭事件
    void closeEvent(QCloseEvent *event) override;
    // 绘制窗口
    void paintEvent(QPaintEvent *event) override;
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event) override;
    // 鼠标移动
    void mouseMoveEvent(QMouseEvent *event) override;
    // 鼠标释放
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // 初始化所有按钮图标与左侧栏水印
    void initIcons();
    // 切换语音按钮图标
    void setAudioBtnRecording(bool recording);
    // 初始化系统托盘图标与菜单
    void initTrayIcon();
    // 由显式退出入口触发，执行真正退出
    void quitApplicationFromUi();
    // 从托盘恢复窗口
    void showFromTray();
    // 从托盘点击位置恢复窗口（点击点作为窗口左下角，并向右上偏移）
    void showFromTrayAt(const QPoint &globalClickPos);
    // 初始化头像
    void initAvatar();
    // 初始化好友列表
    void setupFriendList();
    // 初始化聊天列表
    void setupTalkList();
    // 初始化菜单
    void setupMenu();
    // 初始化云盘页面
    void setupCloudPage();
    // 初始化AI页面
    void setupAiPage();
    // 加载AI助手配置
    void loadAiProviders();
    // 保存AI助手配置
    void saveAiProviders();
    // 重新构建AI助手页面
    void rebuildAiProviderButtons();
    // 打开AI提供商管理对话框
    void openAiProvidersDialog();
    // 获取用户AI配置键名
    QString userAiKey(const QString &suffix) const;
    // 加载主题设置
    void loadThemeSettings();
    // 导入主题图片
    QString importThemeImage(const QString &srcPath, const QString &destBaseName);
    // 获取用户主题配置键名
    QString userThemeKey(const QString &suffix) const;
    // 保存主题设置
    void saveThemeSetting(const QString &key, const QString &value);
    // 应用程序背景
    void applyAppBackground(const QString &pathOrEmpty);
    // 应用聊天背景
    void applyChatBackground(const QString &pathOrEmpty);
    // 初始化语音配置
    void setupAudioSettings();
    // 设置发送按钮位置
    void positionSendButton();
    // 创建用户目录
    QString createUserDirs(const QString& account);
    // 删除用户数据库
    void destroyUserDatabase(const QString& account);
    // 清理缓存
    void clearAccountCache(const QString& account);
    // 初始化消息列表
    void setupMessageList(QListWidget *list);
    // 发送初始化请求
    void havelogin(const QString& account);
    // 把某个好友添加到聊天列表
    void addSomeoneInFriendList(const AccountInfo &friendInfo);
    // 删除好友
    bool deleteSomeoneInFriendList(const QString &account);
    // 删除聊天
    bool deleteSomeoneInTalkList(const QString &account);
    // 判断聊天是否已打开
    bool ifTalkHaveOpened(const QString &account);
    // 添加聊天到列表
    int addSomeoneToTalkList(const AccountInfo &friendMessage,
                             const QString& message,
                             const QString& msgType,
                             const QString& date,
                             const QString& unread);
    // 打开聊天
    int selectSomeoneInTalkList(const QString &account);
    // 发送消息
    void sendMessageToFriend(const QString &account);
    // 切换聊天页面
    bool switchPageTo(const QString &friendId);
    // 获取某个聊天页面
    QWidget* getPage(const QString &friendId);
    // 删除某个聊天页面
    bool deleteTalkWidget(const QString &account);
    // 处理时间戳
    QString processTimestamp(const QString& rawTimestamp);
    // 设置换行
    void resetText(QLabel *label, const QString &text);
    // 添加消息到页面
    void addMessageTo(const QWidget *page, const QString &sender, const QString &receiver,
                      const QString &messageType,const QString &message, const QString& priTimestamp,
                      const QString& uploadTimeStamp, const QString &uuid, bool save,
                      bool insertAtTop = false, bool enableTime = true, bool fileUnavailable = false);
    // 保存图片
    QString savePic(bool save, const QString &friendId, const QPixmap& pix, const QString& messageId);
    // 保存语音
    QString saveAudio(bool save, const QString &friendId, const QByteArray& audioData, const QString& messageId);
    // 把聊天记录添加到本地数据库（若 uuid 本会话已入队则返回 false，避免未读同步 + 实时推送重复）
    bool addMessageToDatabase(const QString &sender,const QString &receiver, const QString &messageType,
                              const QString &message, const QString& priTimestamp, const QString& uploadTimeStamp,
                              const QString &uuid);
    // 把聊天记录添加到本地数据库(等待服务器回发确认后)
    void toAddMessageToDatabase(const QString &sender,const QString &receiver, const QString &messageType,
                                            const QString &message,  const QString& priTimestamp, const QString& uploadTimeStamp, const QString &uuid);
    // 判断是否显示时间戳
    bool printTimeOrNot(const QString& messageTime, const QString& preMessageTime, QString& result);
    // 格式化时间
    void formatMessageTime(const QDateTime& messageTimestamp, QString& result);
    // 更新某人的消息列表信息
    void updateTalkList(const QString& friendId);
    // 更新某人的消息列表信息 指定更新
    void updateTalkListFree(const QString& friendId, const QString& latestMessage, const QString& timestamp);
    // 使某人的未读消息条数加一
    int updateUnread(const QString& friendId);
    // 清空某人的未读消息
    void clearUnread(const QString& friendId);
    // 将 m_talkCache 同步写入主库 talks 表（关窗口 / 清未读后立即落盘，避免与下次登录、未读包叠加）
    void flushTalksCacheToDatabase();
    // 把某人从消息列表提升到最上面
    void liftSomebody(const QString& friendId);
    // 清空某人聊天记录缓存
    void clearSomebodyTalkMessages(const QString& friendId);
    // 获取录制的音频时长
    QString getAudioTime(const QByteArray& audioData);
    // 开始下载切换界面显示
    void setDownLoad(const QString& path);
    // 结束下载切换界面显示
    void clearDownLoad();
    // 更新网盘弹窗内下载进度
    void notifyCloudDriveDownloadProgress(int percent);
    // 将字节数与耗时格式化为 MB/s 文本
    QString formatTransferSpeedMbps(qint64 bytes, qint64 elapsedMs) const;
    // 结束网盘弹窗内下载状态
    void finishCloudDriveDownload(const QString &status);
    // 开始上传切换界面显示
    void setUpload(const QString& path);
    // 结束上传切换界面显示
    void clearUpload();
    // 设置按钮进度
    void setButtonProgress(QPushButton *btn, int percent, const QString &baseTip);
    // 清除按钮进度
    void clearButtonProgress(QPushButton *btn, const QString &baseTip);
    // 播放音效
    void playSound();
    // 二进制转uuid
    QString binaryUuidToString(const QByteArray& binaryUuid);
    // 处理某个待插入的消息记录
    void pushMessageToDatabase(const QString& uuid);
    // 为乐观发送的消息登记 messagehavedone（uuid）截止时刻，启动扫描定时器
    void registerPendingOutboundAckWatch(const QString &uuid);
    // 收到服务端确认或放弃监视时移除对应截止记录
    void clearPendingOutboundAckWatch(const QString &uuid);
    // 周期检查：超时仍未确认的从 m_messageHash 与界面收回
    void sweepPendingOutboundAckTimeouts();
    // 单条处理：移除缓存、重载对端已打开会话页并 flush talks
    void handlePendingOutboundAckTimeout(const QString &uuid);
    // 清理头像缓存目录，每个账号仅保留最新的头像文件
    bool cleanAvatarCache(const QString& avaDir);
    // 某账号头像文件更新后：清内存缓存并标记其消息气泡需懒刷新（进入/切换会话页时再更新控件）
    void refreshMessageAvatarsForAccount(const QString& account);
    // 只扫描单个聊天页上的消息行，更新仍标记为过期的账号气泡头像
    void refreshLazyMessageAvatarsOnPage(QWidget* page);
    // 消息 uuid 规范化为无大括号键，供去重
    QString messageUuidKey(const QString& raw) const;

private:
    // 加载登录数据第一步（请求需下载头像）
    void upload0(const QJsonObject& json);
    // 加载登录数据第二步（好友列表、消息列表、消息记录）
    void upload1(const QJsonObject& json);
    // 加载登录数据第三步（好友申请、未读消息）
    void upload2(const QJsonObject& json);
    // 保存头像
    bool saveAvatarFromBase64(const QJsonObject& friendObject, const QString& avaDir);
    // 加载好友列表
    void uploadFriendList(const QJsonArray &friendsArray);
    // 加载好友申请
    void uploadFriendRequest(const QJsonArray &friendsRequestArray);
    // 加载消息列表
    void uploadListMessages();
    // 加载消息记录
    void uploadMessages();
    // 加载个人的消息记录
    bool loadMessagesForAccount(QWidget *talkPage, const QString &account);
    // 向上滚动时加载更早的消息
    void loadOlderMessagesForAccount(QWidget *talkPage, const QString &account);
    // 从 DB 重载某会话页（清除 uuid 去重键后 load，避免移除会话再打开列表空白）
    void reloadTalkPageFromDatabase(const QString& account);
    // 按 messages/talks 表校正左侧会话预览（去掉断线期间仅乐观更新、未落库的内容）
    void restoreTalkListPreviewFromDatabaseForFriend(const QString& friendId);
    // 丢弃未收到服务器确认的待发消息缓存（视为发送失败）
    void discardPendingOutboundMessagesAfterReconnect();
    // 重连且 askforloginmes 同步完成后：清待发缓存并对 m_messageHash 涉及的已打开会话页按库重载
    void refreshTalkPagesAfterReconnectSync();
    void removeRecentMessageUuidKeysForFriend(const QString& friendAccount);
    // 加载未读聊天记录
    QSet<QString> uploadUnreadMessages(const QJsonArray& messagesArray);
    // 删除好友成功
    void deleteSucceed(const QJsonObject& json);
    // 删除失败
    void deleteFail(const QJsonObject& json);
    // 搜索好友的结果
    void sendSearch(const QJsonObject& json);
    // 修改用户信息的结果
    void changeMyInfo(const QJsonObject& json);
    // 修改账号密码第一个申请的结果
    void changePasswordAns1(const QJsonObject& json);
    // 修改账号密码第二个申请的结果
    void changePasswordAns2(const QJsonObject& json);
    // 注销的结果
    void logoutAns(const QJsonObject& json);
    // 处理好友申请的结果
    void dealFriendsRequest(const QJsonObject& json);
    // 接收到新的好友申请
    void dealNewAddRequest(const QJsonObject& json);
    // 好友申请通过
    void dealAddRequestPass(const QJsonObject& json);
    // 添加某好友信息
    void dealFriendInfo(const QJsonObject& json);
    // 处理被删除好友
    void dealYouAreDeleted(const QJsonObject& json);
    // 处理被挤下线
    void dealYouAreKickedOffline(const QJsonObject& json);
    // 处理接收到的聊天消息
    void dealMessages(const QJsonObject& json);
    // 处理已读消息回执
    void dealMessageHaveRead(const QJsonObject& json);
    // 好友更新个人信息
    void dealFriendChangeInfor(const QJsonObject& json);

private slots:
    // 文档下载流（JSON + 二进制分片严格按序投递，见 SocketDocRead::docStreamPacket）
    void onDocStreamPacket(const QByteArray& data, bool isBinary);
    // 读取服务器发送的文件
    void onReadyReadDoc(const QByteArray& data);
    // 读取文件分片
    void onReadyReadDocBinary(const QByteArray& data);
    // 读取服务器回复数据
    void onReadyRead(const QByteArray& data);
    // 点击主窗口最小化按钮
    void on_but_minwindow_clicked();
    // 点击主窗口最大化按钮
    void on_but_maxwindow_clicked();
    // 点击主窗口关闭按钮
    void on_but_deletewindow_clicked();
    // 跳转到聊天窗口页面
    void on_but_chat_clicked();
    // 跳转到联系人页面
    void on_but_friends_clicked();
    // 切换到云盘页面
    void on_but_cloud_clicked();
    // 切换到ai助手页面
    void on_but_ai_clicked();
    // 输入框改变内容判断能否发送消息
    void on_edit_input_textChanged();
    // 页面发生改变
    void on_pages_currentChanged(int arg1);
    // 点击添加好友按钮（主界面）
    void on_but_addfriends_clicked();
    // 点击添加好友按钮（联系人页）
    void on_but_add0_clicked();
    // 搜索输入更新好友列表视图
    void on_line_search2_textChanged(const QString &arg1);
    // 搜索输入更新聊天列表视图
    void on_line_search_textChanged(const QString &arg1);
    // 发送信息
    void sendMessage();
    // 发送图片
    void on_but_tool_sendpic_clicked();
    // 发送文件
    void on_but_tool_sendfile_clicked();
    // 开始录音
    void on_but_tool_sendaudio_pressed();
    // 停止录音并发送
    void on_but_tool_sendaudio_released();
    // 向服务器发送搜索用户信息申请
    void goSearchFriends(const QString &account);
    // 清理右边好友信息视图
    void clearEditShow2();
    // 更新右侧好友信息视图（普通好友）
    void updateEditShow2Normal(const QModelIndex &index);
    // 更新右侧好友信息视图（好友申请列表）
    void updateEditShow2New();
    // 删除好友
    void deleteFriend();
    // 添加好友
    void goAddFriends(const QString &friendAccount);
    // 弹出修改个人资料窗口
    void changeInfo();
    // 聊天列表菜单选择完毕
    void listtalkChoice(const QJsonObject& json);
    // 发送注销申请
    void goLogout(const QJsonObject &json);
    // 拒绝好友申请
    void rejectAddFriends(const QString &account, const QString &sender);
    // 接受好友申请
    void acceptAddFriends(const QString &account, const QString &sender);
    // 播放语音
    void startAudio(const QString &audioPath);
    // 停止播放录音
    void stopAudio();
    // 打开网盘窗口
    void openCloudDrive();
    // 网盘上传
    void startCloudUpload();
    // 查询网盘文件
    void searchCloudFile(const QString &fileId);
    // 下载网盘文件
    void downloadCloudFile(const QString &fileId, const QString &suggestedFilename = QString());
    // 查询我的网盘文件
    void listMyCloudFiles();
    // 处理网盘查询结果
    void dealCloudSearchResult(const QJsonObject &json);
    // 处理我的网盘文件列表
    void dealListMyCloudFilesResult(const QJsonObject &json);
    // 发送文件
    void sendDoc(const QByteArray &jsonData, const QString &filename, const QString &timestamp, const QString &receiver, const QString& uuid);
    // 上传文件到网盘
    void sendCloudDoc(const QString &filepath, const QString &filename, const QString &timestamp, const QString &uuid);
    // 发送文件保存完毕
    void handleSaveDone(const QString &status);
    // 聊天页面项切换更新消息框视图
    void onTalkItemCurrentChanged();
    // 好友列表项切换更新右侧视图
    void onFriendItemCurrentChanged();
    // 给服务器发送消息
    void sendMessageToServer(const QJsonObject& ject);
    // 定时更新talks表
    void onTimerFlushTalksToDb();
    // 主连接恢复：关闭提示并重新走 askforloginmes0 同步会话
    void onMainSocketConnected();
    // 主连接错误：不退出主窗口，交由 SocketOnly 退避重连
    void onMainSocketError(const QString &errorString);
    // 主连接重试达到上限：允许关闭提示并在关闭后退出客户端
    void onMainSocketReconnectFailed(int retryCount);

signals:
    // 删除完成
    void deleteDone();
    // 搜索结果
    void searchResult(const QJsonObject &json);
    // 修改结果
    void changeResult(const QJsonObject &json);
    // 修改密码第一步结果
    void changePasswordAnswer1(const QJsonObject& json);
    // 修改密码第二步结果
    void changePasswordAnswer2(const QJsonObject& json);
    // 注销结果
    void logoutAnswer(const QJsonObject& json);
    // 保存完成
    void saveDone(const QString& status);
    // 加载完成
    void loadFinishedSig();

private:
    Ui::MainWindow *ui;
    // 接收状态
    RecvState m_recvState = RecvState::WaitHeader;
    // 协议头解析出的待接收数据长度
    uint32_t m_expectedDataLen = 0;
    // 用户自己的信息
    AccountInfo m_myInfo;
    // 好友申请列表
    QVector<AccountInfo> m_newFriendArray;
    // 存储好友列表项
    QHash<QString, QStandardItem*> m_friendItemHash;
    // 存储聊天列表项
    QHash<QString, QStandardItem *> m_talkListItems;
    // 存储聊天页面
    QHash<QString, QWidget *> m_talksItem;
    QHash<QString, QVariantMap> m_talkCache;
    //QVariantMap里存这些字段：
    //"unread" → 未读数(int)、"latest_msg" → 最新消息文本(QString)、
    //"timestamp" → 最新消息时间(QString)
    // 定时刷库定时器
    QTimer *m_flushDbTimer;
    // AI 分析「请稍等」提示（收到结果后关闭）
    QPointer<Dialog> m_aiWaitDialog;
    // 登录后由 loginmessage2 下发的会话令牌，调用 AI 分析时随请求带上
    QString m_aiSessionToken;
    // 客户端侧：禁止在上一轮结果返回前重复发起分析
    bool m_aiAnalysisInProgress = false;
    // 默认头像
    QPixmap m_defaultAva = QPixmap(":/pictures/suliao_avator_normal.jpg");
    // 读取数据缓存区
    QByteArray m_recvBuffer;
    // 用户数据库
    QSqlDatabase m_db;
    // 语音与图片缓存文件夹
    QString m_apDir;
    // 好友头像缓存文件夹
    QString m_avaDir;
    // 输入校验器
    QRegularExpressionValidator *m_validator;
    // 空聊天页面
    QWidget *m_emptyTalkPage = nullptr;
    // 拖动位置
    QPoint m_dragPosition;
    // 待删除好友账号
    QString m_deleteFriendNum;
    // 文件保存路径
    QString m_savePath;
    // 当前下载文件
    QFile m_downloadingFile;
    // 当前下载文件uuid
    QString m_downloadingUuid;
    // 期望接收的下一片序号（下载分片严格连续校验）
    qint64 m_downloadingNextSeq = 0;
    // 已下载字节数
    qint64 m_downloadingBytes = 0;
    // 总字节数
    qint64 m_downloadingTotalBytes = 0;
    // 上次下载进度
    int m_lastDownloadProgressPercent = -1;
    // 上次上传进度（由 upload_ack.received_bytes 驱动）
    int m_lastUploadProgressPercent = -1;
    // 上传/下载速度统计起点（毫秒时间戳）
    qint64 m_uploadTransferStartMs = 0;
    qint64 m_downloadTransferStartMs = 0;
    // 上传文件信息
    QJsonObject m_uploadFile;
    // 上传按钮基础样式
    QString m_uploadBtnBaseStyle;
    // 下载按钮基础样式
    QString m_downloadBtnBaseStyle;
    // 程序背景路径
    QString m_appBackgroundPath;
    // 聊天背景路径
    QString m_chatBackgroundPath;
    // 程序背景缓存
    QPixmap m_appBackgroundPixmap;

    struct AiProvider {
        QString name;
        QString url;
    };
    QVector<AiProvider> m_aiProviders;
    QVector<QPushButton*> m_aiProviderButtons;
    int m_selectedAiIndex = 0;
    // 是否窗口最大化
    bool m_maxFlag = 0;
    // 是否正在拖动
    bool m_moveFlag = 0;
    // 是否已打开添加好友窗体
    bool m_addFriendsFlag = 0;
    // 是否已打开修改个人信息窗体
    bool m_changeInfoFlag = 0;
    // 是否已打开修改密码窗体
    bool m_changePassFlag = 0;
    // 是否已打开注销窗体
    bool m_logoutFlag = 0;
    // 是否调整窗口大小
    bool m_resizeFlag = 0;
    // 添加好友窗口
    AddFriends* m_dialogAdd;
    // 修改个人信息窗口
    ChangeInformation* m_dialogChangeInfo;
    // 修改密码窗口
    ChangePassword *m_dialogChangePass;
    // 注销窗口
    Logout *m_logout;
    // 网盘窗口
    CloudDrive *m_cloudDrive = nullptr;
    // 当前下载是否由网盘弹窗发起
    bool m_cloudDriveDownloading = false;
    // 好友列表主布局
    QVBoxLayout *m_newLayout;
    // 好友列表项模型
    QStandardItemModel *m_friendModel;
    // 好友列表筛选模型
    FilterProxyModel *m_filterProxyModel;
    // 聊天列表项模型
    QStandardItemModel *m_talkModel;
    // 聊天列表筛选模型
    TalkFilterProxyModel *m_talkFilterProxyModel;
    // 音频格式
    QAudioFormat m_format;
    // 录制音频源
    QAudioSource *m_audioSource = nullptr;
    // 录制语音缓冲区
    QBuffer *m_audioBuffer = nullptr;
    // 录音定时器
    QTimer* m_audioTimer = nullptr;
    // 播放语音用
    QAudioSink* m_audioSink = nullptr;
    // 播放语音设备
    QIODevice* m_audioOutput;
    // 播放语音哈希
    QByteArray m_playingAudioHash;
#ifdef HAS_WEBENGINE
    // AI 网页
    QWebEngineView *m_webView;
#endif
    // 音效对象
    QSoundEffect* m_sound;
    // 是否播放音效
    bool m_soundFlag = true;
    // 最大好友数
    int MAX_FRIENDS = 500;
    // 待入队的数据库记录
    QHash<QString, MessageData> m_messageHash;
    // 本会话已展示或已入队写入的消息 uuid（防登录未读拉取与 yourmessages 重复）
    QSet<QString> m_recentMessageUuidKeys;
    // 乐观发送后等待 messagehavedone 的截止时刻（ms 时间戳）；key 为 uuid 字符串
    QHash<QString, qint64> m_pendingMessageAckDeadlineMs;
    // 扫描 m_pendingMessageAckDeadlineMs 是否到期
    QTimer *m_pendingMessageAckSweepTimer = nullptr;
    // 聊天消息每页条数
    static const int CHAT_PAGE_SIZE;
    // 会话最早消息时间
    QMap<QString, QString> m_chatOldestLoadedTime;
    // 会话最早消息id
    QMap<QString, QByteArray> m_chatOldestLoadedId;
    // 会话是否还有更早消息
    QMap<QString, bool> m_chatHasMoreOlder;
    // 会话是否正在加载更早消息
    QMap<QString, bool> m_chatLoadingOlder;
    // 每个会话上一条消息时间
    QMap<QString, QString> m_lastMsgTime;
    // 数据库插入队列
    ThreadSafeQueue<MessageData>* m_msgQueue;
    // 更新消息列表队列
    ThreadSafeQueue<TalksData>* m_talksQueue;
    // 数据库工作线程
    DbWorkerThread* m_dbWorkerThread;
    // 某好友的离线消息
    QHash<QString, QVector<QJsonObject>> m_friendMes;
    static const QString BTN_SELECTED_STYLE;
    static const QString BTN_UNSELECTED_STYLE;
    static const QString BTN_SELECTED_STYLE_SEC;
    static const QString BTN_UNSELECTED_STYLE_SEC;
    // 消息气泡头像需与磁盘对齐的账号（懒更新：仅在实际展示的聊天页上重载 pixmap）
    QSet<QString> m_lazyStaleMessageAvatarAccounts;
    // 自己换头像后，已做过懒刷新的会话页（集齐全部会话页后可清除「自己」的过期标记）
    QSet<QWidget*> m_lazySelfAvatarPagesRefreshed;
    // 文件下载写入失败时统一断开流、删半成品并复位 UI
    void abortDocumentDownloadIoError(const QString& detail);
    // 断网/下载连接断开：取消进行中的文件接收，避免 m_savePath 一直占用无法再次下载
    void cancelDocumentDownloadDueToNetwork(const QString &reason);
    // 根据「在线 + 输入非空」统一刷新发送按钮，避免断网后仍显示可点
    void refreshSendButtonState();
    // 主连接不可用：禁用发送区与附件按钮，并中断进行中的下载
    void applyNetworkOfflineUi();
    // 主连接恢复：按当前选中会话恢复输入区与按钮可用性
    void applyNetworkOnlineUi();
    // 已登录且尚无提示时弹出不可关闭的重连说明（与 socketDisconnected / socketError 共用）
    void showReconnectTipDialogIfNeeded();

    // 首次延迟 havelogin 执行完成后才处理「重连后再同步」，避免与启动流程打架
    bool m_didFirstHavelogin = false;
    // 登录状态下曾断线：下次 upload2 完成后执行 refreshTalkPagesAfterReconnectSync
    bool m_needResyncTalkPagesAfterReconnect = false;
    QTimer *m_loginResyncDebounce = nullptr;
    QPointer<Dialog> m_reconnectTipDialog;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayUserAction = nullptr;
    bool m_forceQuit = false;
    bool m_trayTipShown = false;
    bool m_trayHideAnimating = false;
};

#endif //MAINWINDOW_H
