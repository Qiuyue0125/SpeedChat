/**
 * @file CloudDrive.cpp
 * 速聊网盘页面：上传获 file_id、凭 ID 查询/下载、展示本人上传列表。
 * 业务发包由 MainWindow 监听 cloud*Requested 信号完成；本类仅负责 UI 与状态展示。
 */
#include "CloudDrive.h"
#include "ui_CloudDrive.h"
#include "ChatScrollStyle.h"
#include <QIcon>
#include <QListWidgetItem>
#include <QPainterPath>
#include <QPen>
#include <QScrollBar>

namespace {

// 文件名最大显示长度（超过则截断，末尾加"…"）
static const int kMaxDisplayFileNameLen = 40;

// 截断过长的文件名
QString elideFilename(const QString &name)
{
    if (name.length() > kMaxDisplayFileNameLen) {
        return name.left(kMaxDisplayFileNameLen - 3) + QStringLiteral("…");
    }
    return name;
}

// 将字节数格式化为 B/KB/MB/GB 展示文本
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    if (bytes < 1024LL * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GB");
}
}  // namespace

// 构造：文件 ID 输入样式、列表与查询区滚动条
CloudDrive::CloudDrive(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CloudDrive)
{
    ui->setupUi(this);

    ui->line_file_id->clearFocus();
    ui->but_download->setEnabled(false);
    ui->label_upload_status->setText(QStringLiteral("暂无上传"));
    ui->label_upload_id->setText(QStringLiteral("文件名：---\n文件 ID：---"));
    ui->label_download_status->setText(QStringLiteral("暂无下载"));

    const auto updateFileIdLineStyle = [this]() {
        const bool hasText = !ui->line_file_id->text().trimmed().isEmpty();
        ui->line_file_id->setStyleSheet(
            QStringLiteral("border: 4px solid rgba(255, 153, 179, 1);"
                           "border-radius: 10px;"
                           "background: rgb(255, 250, 252);"
                           "padding: 5px;"
                           "font: 11pt \"Microsoft YaHei UI\";"
                           "color: %1;")
                .arg(hasText ? QStringLiteral("#222") : QStringLiteral("grey")));
    };
    connect(ui->line_file_id, &QLineEdit::textChanged, this, updateFileIdLineStyle);
    updateFileIdLineStyle();

    ui->edit_file_info->verticalScrollBar()->setStyleSheet(chatScrollBarVerticalStyleSheet());
    ui->list_my_files->verticalScrollBar()->setStyleSheet(chatScrollBarVerticalStyleSheet());

    connect(ui->line_file_id, &QLineEdit::returnPressed, this, &CloudDrive::on_but_search_clicked);
    connect(ui->list_my_files, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        onMyFileItemClicked();
    });
    connect(ui->list_my_files, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onMyFileItemClicked();
        on_but_download_clicked();
    });
}

// 析构函数
CloudDrive::~CloudDrive()
{
    delete ui;
}

// 绘制事件
void CloudDrive::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}

// 鼠标按下
void CloudDrive::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
}

// 鼠标移动
void CloudDrive::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);
}

// 鼠标释放
void CloudDrive::mouseReleaseEvent(QMouseEvent *event)
{
    QWidget::mouseReleaseEvent(event);
}

// 关闭事件
void CloudDrive::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
}

// 关闭按钮（已弃用，保留空函数体）
void CloudDrive::on_but_deletewindow_clicked()
{
}

// 上传按钮：发起上传
void CloudDrive::on_but_upload_clicked()
{
    emit cloudUploadRequested();
}

// 查询按钮：校验 file_id 后发起查询
void CloudDrive::on_but_search_clicked()
{
    const QString fileId = ui->line_file_id->text().trimmed();
    if (fileId.isEmpty()) {
        showSearchMessage(QStringLiteral("请输入文件 ID"));
        return;
    }
    m_lastFoundFileId.clear();
    m_lastFoundFilename.clear();
    ui->but_download->setEnabled(false);
    emit cloudSearchRequested(fileId);
}

// 下载按钮：以最近查询/选中的 file_id 发起下载
void CloudDrive::on_but_download_clicked()
{
    const QString fileId = m_lastFoundFileId.isEmpty() ? ui->line_file_id->text().trimmed() : m_lastFoundFileId;
    if (fileId.isEmpty()) {
        showSearchMessage(QStringLiteral("请先输入并查询有效的文件 ID"));
        return;
    }
    emit cloudDownloadRequested(fileId, m_lastFoundFilename);
}

// 请求刷新「我的上传」列表（先显示加载占位再 emit 信号）
void CloudDrive::refreshMyFilesList()
{
    // 防止快速重复刷新
    if (m_loadingMyFiles) return;
    m_loadingMyFiles = true;
    ui->list_my_files->clear();
    QListWidgetItem *loading = new QListWidgetItem(QStringLiteral("正在加载…"));
    loading->setFlags(Qt::NoItemFlags);
    ui->list_my_files->addItem(loading);
    emit cloudListMyFilesRequested();
}

// 刷新「我的上传」按钮
void CloudDrive::on_but_refresh_my_clicked()
{
    refreshMyFilesList();
}

// 点击「我的上传」列表项：将选中文件回填到查询区
void CloudDrive::onMyFileItemClicked()
{
    QListWidgetItem *item = ui->list_my_files->currentItem();
    if (!item) {
        return;
    }
    const QString fileId = item->data(Qt::UserRole).toString();
    if (fileId.isEmpty()) {
        return;
    }
    const QString owner = item->data(Qt::UserRole + 4).toString();
    applySelectedFile(fileId,
                      item->data(Qt::UserRole + 1).toString(),
                      item->data(Qt::UserRole + 2).toString(),
                      item->data(Qt::UserRole + 3).toString(),
                      owner);
}

// 将选中条目同步到查询区与下载上下文
void CloudDrive::applySelectedFile(const QString &fileId, const QString &filename,
                                   const QString &fileSize, const QString &timestamp,
                                   const QString &owner)
{
    const qint64 sizeBytes = fileSize.toLongLong();
    const QString sizeText = formatFileSize(sizeBytes);
    m_lastFoundFileId = fileId;
    m_lastFoundFilename = filename;
    ui->line_file_id->setText(fileId);
    QString info = QStringLiteral("文件名：%1\n文件 ID：%2\n大小：%3\n上传时间：%4")
                       .arg(elideFilename(filename), fileId, sizeText, timestamp);
    if (!owner.isEmpty()) {
        info += QStringLiteral("\n上传者：%1").arg(owner);
    }
    ui->edit_file_info->setPlainText(info);
    ui->but_download->setEnabled(true);
}

// 切换上传中状态（禁用上传按钮、更新提示文案）
void CloudDrive::setUploadInProgress(bool inProgress)
{
    ui->but_upload->setEnabled(!inProgress);
    if (inProgress) {
        m_uploadFinished = false;
        m_lastUploadProgressPercent = 0;
        m_uploadingFilename.clear();
        ui->label_upload_status->setText(QStringLiteral("准备上传…"));
    }
}

// 更新上传进度与速率展示
void CloudDrive::setUploadProgress(int percent, const QString &speedText, const QString &filename)
{
    if (m_uploadFinished) {
        return;
    }
    if (!filename.isEmpty()) {
        m_uploadingFilename = filename;
    }
    if (percent >= 100) {
        percent = 100;
    } else {
        percent = qBound(0, percent, 99);
    }
    m_lastUploadProgressPercent = percent;

    // 第一行：文件名（太长则截断省略）
    const QString dispName = elideFilename(m_uploadingFilename);
    // 第二行：进度百分比 + 速率
    QString infoLine = QStringLiteral("%1%").arg(percent);
    if (!speedText.isEmpty()) {
        infoLine += QStringLiteral("  ·  %1").arg(speedText);
    }
    ui->label_upload_status->setText(dispName + QStringLiteral("\n") + infoLine);
}

// 切换下载中状态
void CloudDrive::setDownloadInProgress(bool inProgress, const QString &filename)
{
    ui->but_download->setEnabled(!inProgress);
    if (!inProgress) {
        m_downloadingFilename.clear();
        ui->label_download_status->setText(QStringLiteral("暂无下载"));
        return;
    }
    m_downloadingFilename = filename;
    const QString dispName = filename.isEmpty() ? QStringLiteral("文件") : elideFilename(filename);
    ui->label_download_status->setText(dispName + QStringLiteral("\n0%"));
}

// 更新下载进度与速率展示
void CloudDrive::setDownloadProgress(int percent, const QString &speedText, const QString &)
{
    percent = qBound(0, percent, 100);
    const QString dispName = m_downloadingFilename.isEmpty() ? QStringLiteral("文件") : elideFilename(m_downloadingFilename);
    QString infoLine = QStringLiteral("%1%").arg(percent);
    if (!speedText.isEmpty()) {
        infoLine += QStringLiteral("  ·  %1").arg(speedText);
    }

    ui->label_download_status->setText(dispName + QStringLiteral("\n") + infoLine);
}

// 下载结束（根据 status 是否含「成功」切换完成/失败文案）
void CloudDrive::finishDownload(const QString &status)
{
    m_downloadingFilename.clear();
    if (status.contains(QStringLiteral("成功"))) {
        ui->label_download_status->setText(QStringLiteral("下载完成"));
    } else {
        ui->label_download_status->setText(QStringLiteral("下载失败"));
    }
    ui->but_download->setEnabled(!m_lastFoundFileId.isEmpty());
}

// 上传成功：展示 file_id 供用户复制分享
void CloudDrive::showUploadedFileId(const QString &fileId, const QString &filename)
{
    m_uploadFinished = true;
    m_lastUploadProgressPercent = 100;
    ui->label_upload_status->setText(QStringLiteral("上传成功"));

    ui->label_upload_id->setText(
        QStringLiteral("文件名：%1\n文件 ID：%2").arg(elideFilename(filename), fileId));
    ui->but_upload->setEnabled(true);
}

// 展示 searchcloudfile_result 查询结果
void CloudDrive::showSearchResult(const QJsonObject &json)
{
    const bool found = json.value(QStringLiteral("found")).toString() == QStringLiteral("true");
    if (!found) {
        m_lastFoundFileId.clear();
        m_lastFoundFilename.clear();
        ui->but_download->setEnabled(false);
        ui->edit_file_info->setPlainText(QStringLiteral("未找到该文件，请检查 ID 是否正确。"));
        return;
    }

    const QString fileId = json.value(QStringLiteral("file_id")).toString();
    const QString filename = json.value(QStringLiteral("filename")).toString();
    const QString owner = json.value(QStringLiteral("owner")).toString();
    const QString fileSize = json.value(QStringLiteral("file_size")).toString();
    const QString timestamp = json.value(QStringLiteral("timestamp")).toString();

    const qint64 sizeBytes = fileSize.toLongLong();
    const QString sizeText = formatFileSize(sizeBytes);
    m_lastFoundFileId = fileId;
    m_lastFoundFilename = filename;
    ui->edit_file_info->setPlainText(
        QStringLiteral("文件名：%1\n上传者：%2\n大小：%3\n上传时间：%4")
            .arg(elideFilename(filename), owner, sizeText, timestamp));
    ui->but_download->setEnabled(true);
}

// 展示查询区纯文本提示（如参数错误）
void CloudDrive::showSearchMessage(const QString &text)
{
    ui->edit_file_info->setPlainText(text);
}

// 填充「我的上传」列表
void CloudDrive::showMyFilesList(const QJsonArray &files)
{
    m_loadingMyFiles = false;
    ui->list_my_files->clear();
    if (files.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem(QStringLiteral("暂无上传记录"));
        empty->setFlags(Qt::NoItemFlags);
        ui->list_my_files->addItem(empty);
        return;
    }

    for (const QJsonValue &val : files) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        const QString fileId = obj.value(QStringLiteral("file_id")).toString();
        const QString filename = obj.value(QStringLiteral("filename")).toString();
        const QString owner = obj.value(QStringLiteral("owner")).toString();
        const qint64 sizeBytes = obj.value(QStringLiteral("file_size")).toString().toLongLong();
        const QString timestamp = obj.value(QStringLiteral("timestamp")).toString();
        const QString sizeText = formatFileSize(sizeBytes);

        auto *item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2  ·  %3").arg(elideFilename(filename), sizeText, timestamp));
        item->setData(Qt::UserRole, fileId);
        item->setData(Qt::UserRole + 1, filename);
        item->setData(Qt::UserRole + 2, QString::number(sizeBytes));
        item->setData(Qt::UserRole + 3, timestamp);
        item->setData(Qt::UserRole + 4, owner);
        ui->list_my_files->addItem(item);
    }
}

// 「我的上传」加载失败或空状态提示
void CloudDrive::showMyFilesMessage(const QString &text)
{
    m_loadingMyFiles = false;
    ui->list_my_files->clear();
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setFlags(Qt::NoItemFlags);
    ui->list_my_files->addItem(item);
}