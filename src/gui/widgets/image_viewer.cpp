#include "image_viewer.h"
#include <QApplication>
#include <QGraphicsPixmapItem>
#include <QKeyEvent>
#include <QScreen>
#include <QScrollBar>
#include <QVBoxLayout>

ImageViewerDialog::ImageViewerDialog(QWidget* parent) : QDialog(parent, Qt::Window) {
    setWindowTitle("Waifu Gallery - Image Viewer");
    setAttribute(Qt::WA_DeleteOnClose, false);

    scene = new QGraphicsScene(this);
    pixmapItem = new QGraphicsPixmapItem();
    scene->addItem(pixmapItem);

    graphicsView = new QGraphicsView(scene, this);
    graphicsView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    graphicsView->setDragMode(QGraphicsView::NoDrag);
    graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    graphicsView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    graphicsView->setStyleSheet("QGraphicsView { background: #1a1a2e; border: none; }");

    infoLabel = new QLabel(this);
    infoLabel->setStyleSheet("QLabel { color: #aaa; font-size: 12px; padding: 4px 8px; }");
    infoLabel->setAlignment(Qt::AlignCenter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(graphicsView, 1);
    layout->addWidget(infoLabel);

    graphicsView->viewport()->installEventFilter(this);

    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        resize(screenGeometry.width() * 0.85, screenGeometry.height() * 0.85);
    }

    updateCursor();
}

ImageViewerDialog::~ImageViewerDialog() = default;

void ImageViewerDialog::open(ImageViewerController* ctrl) {
    if (controller) {
        disconnect(controller, nullptr, this, nullptr);
    }
    controller = ctrl;

    connect(controller, &ImageViewerController::imageReady, this, &ImageViewerDialog::onImageReady);
    connect(controller, &ImageViewerController::imageLoading, this, &ImageViewerDialog::onImageLoading);
    connect(controller, &ImageViewerController::indexChanged, this, &ImageViewerDialog::onIndexChanged);
    connect(controller, &ImageViewerController::picInfoChanged, this, &ImageViewerDialog::onPicInfoChanged);
    connect(controller, &ImageViewerController::transformChanged, this, &ImageViewerDialog::onTransformChanged);

    controller->requestCurrentImage();

    QWidget::show();
    activateWindow();
    raise();
}

bool ImageViewerDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched != graphicsView->viewport()) return QDialog::eventFilter(watched, event);

    if (event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            if (wheelEvent->angleDelta().y() > 0) controller->zoomIn();
            else controller->zoomOut();
        } else {
            if (wheelEvent->angleDelta().y() > 0) controller->navigatePrev();
            else controller->navigateNext();
        }
        wheelEvent->accept();
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::MiddleButton || mouseEvent->button() == Qt::LeftButton) {
            isPanning = true;
            lastPanPoint = mouseEvent->pos();
            updateCursor();
            mouseEvent->accept();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove && isPanning) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint delta = mouseEvent->pos() - lastPanPoint;
        lastPanPoint = mouseEvent->pos();
        graphicsView->horizontalScrollBar()->setValue(graphicsView->horizontalScrollBar()->value() - delta.x());
        graphicsView->verticalScrollBar()->setValue(graphicsView->verticalScrollBar()->value() - delta.y());
        mouseEvent->accept();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (isPanning && (mouseEvent->button() == Qt::MiddleButton || mouseEvent->button() == Qt::LeftButton)) {
            isPanning = false;
            updateCursor();
            mouseEvent->accept();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            controller->toggleFitMode();
            mouseEvent->accept();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void ImageViewerDialog::onImageReady(QPixmap pixmap) {
    pixmapItem->setPixmap(pixmap);
    if (controller) onTransformChanged(controller->transform());
}

void ImageViewerDialog::onImageLoading() {
    pixmapItem->setPixmap(QPixmap());
    infoLabel->setText("Loading...");
}

void ImageViewerDialog::onIndexChanged(int index, int total) {
    Q_UNUSED(index);
    Q_UNUSED(total);
    updateInfoLabel();
}

void ImageViewerDialog::onPicInfoChanged(const PicInfo* info, const Metadata* metadata) {
    Q_UNUSED(info);
    Q_UNUSED(metadata);
    updateInfoLabel();
}

void ImageViewerDialog::onTransformChanged(const ViewerTransform& xform) {
    if (xform.fitMode) {
        fitToWindow(xform);
    } else {
        applyTransform(xform);
    }
    updateInfoLabel();
}

void ImageViewerDialog::fitToWindow(const ViewerTransform& xform) {
    if (pixmapItem->pixmap().isNull()) return;
    graphicsView->resetTransform();
    if (xform.hFlip) graphicsView->scale(-1, 1);
    if (xform.vFlip) graphicsView->scale(1, -1);
    if (xform.rotationAngle != 0) graphicsView->rotate(xform.rotationAngle);
    graphicsView->fitInView(pixmapItem, Qt::KeepAspectRatio);
}

void ImageViewerDialog::applyTransform(const ViewerTransform& xform) {
    graphicsView->resetTransform();
    graphicsView->scale(xform.zoomLevel, xform.zoomLevel);
    if (xform.hFlip) graphicsView->scale(-1, 1);
    if (xform.vFlip) graphicsView->scale(1, -1);
    if (xform.rotationAngle != 0) graphicsView->rotate(xform.rotationAngle);
}

void ImageViewerDialog::updateInfoLabel() {
    if (!controller) return;
    const ViewerTransform& xform = controller->transform();
    QStringList parts;

    parts << QString("%1 / %2").arg(controller->currentIndex() + 1).arg(controller->totalCount());

    const PicInfo* info = controller->currentPicInfo();
    if (info) {
        parts << QString("%1x%2").arg(info->width).arg(info->height);
    }
    if (xform.zoomLevel > 0) {
        parts << QString("%1%").arg(qRound(xform.zoomLevel * 100));
    }
    if (xform.rotationAngle != 0) {
        parts << QString("%1°").arg(xform.rotationAngle);
    }
    if (xform.hFlip || xform.vFlip) {
        parts << (xform.hFlip && xform.vFlip ? "HV" : xform.hFlip ? "H" : "V");
    }
    if (xform.animPaused) {
        parts << QString::fromUtf8("\u23F8");
    }

    infoLabel->setText(parts.join("  |  "));
}

void ImageViewerDialog::updateCursor() {
    graphicsView->setCursor(isPanning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
}

void ImageViewerDialog::keyPressEvent(QKeyEvent* event) {
    if (!controller) { QDialog::keyPressEvent(event); return; }

    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        break;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        controller->navigatePrev();
        break;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        controller->navigateNext();
        break;
    case Qt::Key_Home:
        controller->navigateFirst();
        break;
    case Qt::Key_End:
        controller->navigateLast();
        break;
    case Qt::Key_F:
        controller->toggleFitMode();
        break;
    case Qt::Key_1:
        controller->setOriginalSize();
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_F11:
        isFullScreen() ? showNormal() : showFullScreen();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        controller->zoomIn();
        break;
    case Qt::Key_Minus:
        controller->zoomOut();
        break;
    case Qt::Key_H:
        controller->flipHorizontal();
        break;
    case Qt::Key_V:
        controller->flipVertical();
        break;
    case Qt::Key_R:
        controller->rotateCW();
        break;
    case Qt::Key_L:
        controller->rotateCCW();
        break;
    case Qt::Key_P:
        controller->toggleAnimPause();
        break;
    default:
        QDialog::keyPressEvent(event);
    }
}

void ImageViewerDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (controller && controller->transform().fitMode) {
        fitToWindow(controller->transform());
    }
}
