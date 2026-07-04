#ifndef AIANALYZERESULTDIALOG_H
#define AIANALYZERESULTDIALOG_H

/**
 * @file AiAnalyzeResultDialog.h
 * 展示聊天 AI 分析结果。
 */

#include <QDialog>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPoint>

namespace Ui {
class AiAnalyzeResultDialog;
}

// AI 分析结果：与 AiAnalyzeDialog 同风格（云背景、圆角描边、主按钮样式）。
class AiAnalyzeResultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AiAnalyzeResultDialog(QWidget *parent = nullptr);
    ~AiAnalyzeResultDialog() override;

    // 设置展示的大段分析结果正文。
    void setResultText(const QString &text);

protected:
    // 绘制无边框云背景与圆角外框。
    void paintEvent(QPaintEvent *event) override;
    // 鼠标按下：判断是否进入窗口拖动状态。
    void mousePressEvent(QMouseEvent *event) override;
    // 鼠标移动：在拖动状态下移动窗口。
    void mouseMoveEvent(QMouseEvent *event) override;
    // 鼠标松开：退出拖动状态。
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    // 关闭“结果”弹窗（x）。
    void on_but_deletewindow_clicked();
    // 关闭“结果”弹窗（关闭按钮）。
    void on_but_close_clicked();

private:
    Ui::AiAnalyzeResultDialog *ui = nullptr;
    int m_moveFlag = 0;
    QPoint m_dragPosition;

    static const QString BTN_ENABLE_STYLE;
};

#endif
