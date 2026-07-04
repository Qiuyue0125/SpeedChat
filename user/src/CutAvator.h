#ifndef CUTAVATOR_H
#define CUTAVATOR_H

/**
 * @file CutAvator.h
 * 头像裁剪预览对话框。
 */

#include <QDialog>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QString>
#include <QMouseEvent>
#include <QTimer>

namespace Ui {
class CutAvator;
}

class AvatarCropCanvas;

class CutAvator : public QDialog
{
    Q_OBJECT

public:
    // 构造函数
    explicit CutAvator(const QString &path,QWidget *parent = nullptr);
    // 析构函数
    ~CutAvator();

protected:
    // 绘制窗口
    void paintEvent(QPaintEvent *event);

private:
    // 处理图片
    void dealImage(const QString &path);
    // 从图片路径重建“基础画布”（包含白底与居中缩放结果）。
    void rebuildBaseCanvas(const QString &srcPath);
    // 刷新预览：让自绘画布根据 m_baseCanvas + m_labelRect 重绘遮罩。
    void renderPreviewFromBase();
    // 更新背景（入口：从 avapath 重建基础画布并刷新预览）。
    void setBackground(const QString &avapath);

private slots:
    // 更新裁剪框
    void moveRect(const QPoint &newPosition);
    // 点击裁剪
    void on_but_cut_clicked();
    // 点击确认
    void on_but_yes_clicked();

signals:
    // 发送裁剪结果
    void cutOk(const QPixmap &pixmap);

private:
    Ui::CutAvator *ui;
    QString m_mypath;
    QRect m_labelRect;
    // 缓存：基础画布（白底 + 居中缩放图），用于拖动时快速重绘。
    QPixmap m_baseCanvas;

    // 不再使用 ui->dock 做背景 palette；而是用这个自绘 QWidget 显示背景与遮罩。
    AvatarCropCanvas *m_canvas = nullptr;

    // 合并拖动过程的多次刷新请求，避免 paint 事件队列堆积导致“跟不上手”。
    bool m_canvasUpdatePending = false;
    // 如果在一次已排队的更新期间又触发了移动，则标记需要在下一次更新后再补画一次“最新状态”。
    bool m_canvasNeedsAnotherPaint = false;

    friend class AvatarCropCanvas;
};


class DraggableResizableLabel : public QLabel
{
    Q_OBJECT
public:
    // 作为拖拽/缩放选区控件的 Label 容器。
    explicit DraggableResizableLabel(QWidget *parent = nullptr) : QLabel(parent) {}

protected:
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event) override;
    // 鼠标移动
    void mouseMoveEvent(QMouseEvent *event) override;
    // 鼠标释放
    void mouseReleaseEvent(QMouseEvent *event) override;

signals:
    // 发送位置变化
    void labelMoved(const QPoint &newPosition);

private:
    QPoint m_myOffset;
    bool m_myResizing = false;
};

#endif // 结束
