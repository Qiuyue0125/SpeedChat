#ifndef AIANALYZEDIALOG_H
#define AIANALYZEDIALOG_H

/**
 * @file AiAnalyzeDialog.h
 * 选择 AI 分析范围与参数的对话框。
 */

#include <QDialog>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QMouseEvent>
#include <QEvent>
#include <QPaintEvent>
#include <QPoint>
#include <QPushButton>

namespace Ui {
class AiAnalyzeDialog;
}

// 聊天列表「AI分析」：样式与修改密码窗体一致（无边框云背景），单页无 Stack。
class AiAnalyzeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AiAnalyzeDialog(QWidget *parent = nullptr);
    ~AiAnalyzeDialog() override;

    // 设置展示的好友名
    void setPeerDisplayName(const QString &peerName);

    // 时间范围数值（1～999），非法输入视为 0。
    int rangeValue() const;

    // 是否启用“选择具体时间点”模式。
    bool isAbsoluteTimeMode() const;

    // 自定义模式下的开始/结束时间点。
    QDateTime absoluteStartDateTime() const;
    QDateTime absoluteEndDateTime() const;

    // 当前选中的单位键：hour / day / month（仅相对模式使用）。
    QString rangeUnitKey() const;
    // 用户编辑的提示词
    QString promptText() const;

protected:
    // 负责绘制无边框云背景与圆角描边。
    void paintEvent(QPaintEvent *event) override;
    // 拦截键盘输入，避免用户通过输入法直接修改时间；鼠标仍可通过日历/旋钮选择。
    bool eventFilter(QObject *obj, QEvent *event) override;
    // 点击边缘区域时支持拖动窗口。
    void mousePressEvent(QMouseEvent *event) override;
    // 拖动过程中移动窗口。
    void mouseMoveEvent(QMouseEvent *event) override;
    // 结束拖动状态。
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    // “x”关闭按钮：拒绝对话框。
    void on_but_deletewindow_clicked();
    // “开始分析/确认”按钮：校验参数并接受对话框。
    void on_but_confirm_clicked();

private:
    Ui::AiAnalyzeDialog *ui = nullptr;
    int m_moveFlag = 0;
    QPoint m_dragPosition;

    // 绝对时间点控件（来自 ui）。
    QDateTimeEdit *m_dtStart = nullptr;
    QDateTimeEdit *m_dtEnd = nullptr;

    static const QString BTN_ENABLE_STYLE;
};

#endif
