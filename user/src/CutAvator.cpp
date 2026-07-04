/**
 * @file CutAvator.cpp
 * 头像裁剪与预览。
 */
#include "CutAvator.h"
#include "ui_CutAvator.h"
#include "Dialog.h"
#include "RegisterWindow.h"

 // 自绘画布：只负责绘制“背景图片 + 遮罩 + 预览效果”
// 选择框仍然由 ui->lab_cut（DraggableResizableLabel）负责鼠标交互与虚线边框。
class AvatarCropCanvas : public QWidget {
public:
    // 自绘画布：由 owner 提供基础图（m_baseCanvas）与选区（m_labelRect）。
    AvatarCropCanvas(CutAvator *owner, QWidget *parent = nullptr)
        : QWidget(parent), m_owner(owner) {}

protected:
    // 在自绘画布上绘制：背景图 + 遮罩 + 当前选区“抠回去”的预览。
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        if (!m_owner)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        if (m_owner->m_baseCanvas.isNull()) {
            p.fillRect(rect(), QColor(255, 255, 255));
            return;
        }

        // 先画完整背景
        p.drawPixmap(0, 0, width(), height(), m_owner->m_baseCanvas);

        // 遮罩：选区外半透明黑色；选区内保持清晰（通过“先涂遮罩再抠回”实现）
        p.fillRect(rect(), QColor(0, 0, 0, 128));

        const QRect sel = m_owner->m_labelRect.intersected(rect());
        if (sel.isValid()) {
            // 直接从 m_baseCanvas 的源矩形绘制目标矩形，避免每帧 QPixmap::copy 开销。
            p.drawPixmap(sel, m_owner->m_baseCanvas, sel);
        }
    }

private:
    CutAvator *m_owner = nullptr;
};

// 构造函数
CutAvator::CutAvator(const QString &path,QWidget *parent)
    : QDialog(parent),ui(new Ui::CutAvator)
{
    ui->setupUi(this);
    m_mypath = QCoreApplication::applicationDirPath() + QDir::separator() + "saved_background_image.png";

    // 不再依赖旧的背景容器做渲染，改为使用自绘画布叠加在裁剪容器上。
    ui->dock_avator->setFixedSize(277, 277);

    m_canvas = new AvatarCropCanvas(this, ui->dock_avator);
    m_canvas->setGeometry(QRect(0, 0, 277, 277));
    m_canvas->lower();

    ui->lab_cut->raise();
    ui->lab_cut->setStyleSheet(ui->lab_cut->styleSheet() + "\nbackground: transparent;");

    // 处理图片
    dealImage(path);
    // 连接信号
    connect(ui->lab_cut, &DraggableResizableLabel::labelMoved, this, &CutAvator::moveRect);

    // 初始 selection 坐标
    m_labelRect = ui->lab_cut->geometry();

    // 窗口居中
    QWidget *topLevelParent = parent ? parent->window() : nullptr;
    if (topLevelParent) {
        int x = topLevelParent->x() + (topLevelParent->width() - width()) / 2;
        int y = topLevelParent->y() + (topLevelParent->height() - height()) / 2;
        move(x, y);
    }
}

// 析构函数
CutAvator::~CutAvator()
{
    QFile::remove(m_mypath);
    delete ui;
}

// 绘制窗口
void CutAvator::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPixmap pixmap(":/pictures/094 Cloudy Apple - trans.png");
    painter.drawPixmap(0, 0, width(), height(), pixmap);
    QIcon icon(":/pictures/suliao.png");
    setWindowIcon(icon);
    QDialog::paintEvent(event);
}

// 处理图片
void CutAvator::dealImage(const QString &path)
{
    QImage image(path);
    if (image.isNull()) {
        QString text =  "图片创建失败";
        Dialog* dialog = new Dialog(this);
        dialog->transText(text);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    image.save(path, "PNG");
    setBackground(path);
}

// 从 srcPath 重建“基础画布”：白底 + 居中缩放的原图内容。
// 该基础画布会被 renderPreviewFromBase() 用来生成带遮罩的预览，
// 拖动时不会重复缩放/读盘，从而显著降低卡顿。
void CutAvator::rebuildBaseCanvas(const QString &srcPath)
{
    QPixmap srcPix(srcPath);
    if (srcPix.isNull())
        return;

    const QSize targetSize = ui->dock_avator->size().isEmpty() ? ui->lab_cut->size() : ui->dock_avator->size();
    if (targetSize.isEmpty())
        return;

    const QPixmap scaled = srcPix.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    m_baseCanvas = QPixmap(targetSize);
    m_baseCanvas.fill(QColor(255, 255, 255, 255));

    QPainter painter(&m_baseCanvas);
    painter.setRenderHint(QPainter::Antialiasing);
    const int x = (targetSize.width() - scaled.width()) / 2;
    const int y = (targetSize.height() - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);

    m_labelRect = ui->lab_cut->geometry(); // 选择框相对裁剪容器的坐标系
    const QRect canvasRect(QPoint(0, 0), m_baseCanvas.size());
    const QRect safeRect = m_labelRect.intersected(canvasRect);

    // 初始化（以及每次“裁剪”后）更新 saved_background_image.png，给“确认”直接使用。
    if (safeRect.isValid() && safeRect.width() > 0 && safeRect.height() > 0) {
        QPixmap cropped = m_baseCanvas.copy(safeRect);
        cropped.save(m_mypath, "PNG");
    } else {
        // 兜底：选区如果异常，则至少保存当前画布，避免后续“确认”读不到图。
        m_baseCanvas.save(m_mypath, "PNG");
    }
}

// 触发自绘画布刷新（由 AvatarCropCanvas 根据 m_baseCanvas+m_labelRect 画遮罩）。
void CutAvator::renderPreviewFromBase()
{
    if (!m_canvas)
        return;

    // 合并刷新请求：拖动时 mouseMove 可能触发过多更新，导致 paint 队列堆积。
    if (m_canvasUpdatePending) {
        // 已经排队一次刷新了：再来的移动只标记“需要补画最新状态”，
        // 避免 paint 队列堆积，同时尽量减少“半拍落后”。
        m_canvasNeedsAnotherPaint = true;
        return;
    }

    m_canvasUpdatePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_canvasUpdatePending = false;
        if (!m_canvas)
            return;

        // 先画一次当前状态
        m_canvas->update();

        // 如果期间又来了更多移动，则再补画一帧最新状态
        if (m_canvasNeedsAnotherPaint) {
            m_canvasNeedsAnotherPaint = false;
            m_canvas->update();
        }
    });
}

// 更新背景
void CutAvator::setBackground(const QString &avapath)
{
    rebuildBaseCanvas(avapath);
    renderPreviewFromBase();
}

// 更新裁剪框
void CutAvator::moveRect(const QPoint &newPosition)
{
    Q_UNUSED(newPosition);
    m_labelRect = ui->lab_cut->geometry();
    // 直接刷新画布：拖动过程需要更“跟手”，不做节流。
    renderPreviewFromBase();
}

// 点击裁剪
void CutAvator::on_but_cut_clicked()
{
    ui->lab_cut->setVisible(false);

    // 保存当前选区到 m_mypath（确认按钮直接使用这个文件）。
    if (!m_baseCanvas.isNull()) {
        const QRect canvasRect(QPoint(0, 0), m_baseCanvas.size());
        const QRect safeRect = m_labelRect.intersected(canvasRect);
        if (safeRect.isValid() && safeRect.width() > 0 && safeRect.height() > 0) {
            QPixmap cropped = m_baseCanvas.copy(safeRect);
            cropped.save(m_mypath, "PNG");
        } else {
            m_baseCanvas.save(m_mypath, "PNG");
        }
    }

    // 关键：裁剪后把选区恢复到整张 277x277，避免“再次裁剪尺寸/位置错位”。
    ui->lab_cut->setGeometry(QRect(0, 0, 277, 277));
    ui->lab_cut->setFixedSize(277, 277);
    ui->lab_cut->setVisible(true);

    // 重新以新的 m_mypath 作为底图构建，并按新的整选区生成输出文件。
    rebuildBaseCanvas(m_mypath);
    renderPreviewFromBase();
}

// 点击确认
void CutAvator::on_but_yes_clicked()
{
    // 确保“确认”时一定保存当前选区结果，避免用户未点“裁剪”就直接确认导致文件内容过期。
    if (!m_baseCanvas.isNull()) {
        const QRect canvasRect(QPoint(0, 0), m_baseCanvas.size());
        const QRect safeRect = m_labelRect.intersected(canvasRect);
        if (safeRect.isValid() && safeRect.width() > 0 && safeRect.height() > 0) {
            QPixmap cropped = m_baseCanvas.copy(safeRect);
            cropped.save(m_mypath, "PNG");
        } else {
            m_baseCanvas.save(m_mypath, "PNG");
        }
    }
    emit cutOk(m_mypath);
    close();
}

// 鼠标按下
void DraggableResizableLabel::mousePressEvent(QMouseEvent *event)
{
    // 左键按下：记录拖动起点偏移；同时判断是否从右下角附近进入“缩放”模式。
    if (event->button() == Qt::LeftButton)
    {
        m_myOffset = event->pos();
        m_myResizing = (event->pos().x() > width() - 10 && event->pos().y() > height() - 10);
    }
    QLabel::mousePressEvent(event);
}

// 鼠标移动
void DraggableResizableLabel::mouseMoveEvent(QMouseEvent *event)
{
    // 根据鼠标相对控件位置给出缩放光标反馈；
    // 拖动按下期间不随时切换 move/resize 模式，避免手感差异。
    const bool hoverResizeRegion = (event->pos().x() > width() - 10 && event->pos().y() > height() - 10);
    if (hoverResizeRegion)
    {
        setCursor(Qt::SizeFDiagCursor);
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }
    if (event->buttons() & Qt::LeftButton)
    {
        // 进入缩放模式的判断只在“鼠标进入缩放区域”时置为 true；
        // 拖动过程中离开区域也不会立刻切换回移动模式，保证手感一致。
        if (hoverResizeRegion)
            m_myResizing = true;

        if (m_myResizing)
        {
            int newSize = qMax(50, qMin(event->pos().x(), event->pos().y()));
            QRect parentRect = parentWidget()->rect();
            int maxWidth = parentRect.width() - pos().x();
            int maxHeight = parentRect.height() - pos().y();
            newSize = qMin(newSize, qMin(maxWidth, maxHeight));
            setFixedSize(newSize, newSize);
            emit labelMoved(mapToParent(QPoint(0, 0)));
        }
        else
        {
            QPoint newPosition = mapToParent(event->pos() - m_myOffset);
            QRect parentRect = parentWidget()->rect();
            QSize labelSize = size();
            newPosition.setX(qMax(parentRect.left(), qMin(newPosition.x(), parentRect.right() - labelSize.width())));
            newPosition.setY(qMax(parentRect.top(), qMin(newPosition.y(), parentRect.bottom() - labelSize.height())));
            move(newPosition);
            emit labelMoved(newPosition);
        }
    }
    QLabel::mouseMoveEvent(event);
}

// 鼠标释放
void DraggableResizableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    m_myResizing = false;
    unsetCursor();
    QLabel::mouseReleaseEvent(event);

}
