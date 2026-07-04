#ifndef CLOUDDRIVE_H
#define CLOUDDRIVE_H

/**
 * @file CloudDrive.h
 * 速聊网盘对话框：上传获 file_id、凭 ID 查询/下载、展示本人上传列表。
 * 业务发包由 MainWindow 监听 cloud*Requested 信号完成；本类仅负责 UI 与状态展示。
 */

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>

namespace Ui {
class CloudDrive;
}

class CloudDrive : public QWidget
{
    Q_OBJECT

public:
    // 构造函数
    explicit CloudDrive(QWidget *parent = nullptr);
    // 析构函数
    ~CloudDrive() override;

    // 切换上传中状态（禁用上传按钮、更新提示文案）
    void setUploadInProgress(bool inProgress);
    // 更新上传进度与速率展示
    void setUploadProgress(int percent, const QString &speedText = QString(),
                           const QString &filename = QString());
    // 切换下载中状态
    void setDownloadInProgress(bool inProgress, const QString &filename = QString());
    // 更新下载进度与速率展示
    void setDownloadProgress(int percent, const QString &speedText = QString(),
                             const QString &filename = QString());
    // 下载结束（根据 status 是否含「成功」切换完成/失败文案）
    void finishDownload(const QString &status);
    // 上传成功：展示 file_id 供用户复制分享
    void showUploadedFileId(const QString &fileId, const QString &filename);
    // 展示 searchcloudfile_result 查询结果
    void showSearchResult(const QJsonObject &json);
    // 展示查询区纯文本提示（如参数错误）
    void showSearchMessage(const QString &text);
    // 填充「我的上传」列表
    void showMyFilesList(const QJsonArray &files);
    // 「我的上传」加载失败或空状态提示
    void showMyFilesMessage(const QString &text);
    // 请求刷新「我的上传」列表（先显示加载占位再 emit 信号）
    void refreshMyFilesList();

signals:
    // 请求 MainWindow 发起网盘上传（选文件 + 文档通道）
    void cloudUploadRequested();
    // 请求查询 file_id 对应文件元数据
    void cloudSearchRequested(const QString &fileId);
    // 请求下载指定 file_id 到本地
    void cloudDownloadRequested(const QString &fileId, const QString &filename);
    // 请求列出当前账号上传过的网盘文件
    void cloudListMyFilesRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_but_deletewindow_clicked();
    void on_but_upload_clicked();
    void on_but_search_clicked();
    void on_but_download_clicked();
    void on_but_refresh_my_clicked();
    void onMyFileItemClicked();

private:
    // 将选中条目同步到查询区与下载上下文
    void applySelectedFile(const QString &fileId, const QString &filename,
                           const QString &fileSize, const QString &timestamp,
                           const QString &owner = QString());

    Ui::CloudDrive *ui;
    // 最近一次查询/选中有效的 file_id
    QString m_lastFoundFileId;
    // 最近一次查询/选中有效的文件名（下载时作默认保存名）
    QString m_lastFoundFilename;
    // 当前下载展示用文件名
    QString m_downloadingFilename;
    int m_lastUploadProgressPercent = -1;
    // 上传是否已进入完成态（避免进度回调覆盖「上传成功」文案）
    bool m_uploadFinished = false;
    // 当前上传文件名（用于进度展示）
    QString m_uploadingFilename;
    // 是否正在加载「我的上传」列表（防止快速重复刷新）
    bool m_loadingMyFiles = false;
    QPoint m_dragPosition;
    int m_moveFlag = 0;
};

#endif
