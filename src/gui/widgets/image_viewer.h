#pragma once

#include "gui/controllers/image_viewer_controller.h"
#include <QDialog>
#include <QGraphicsView>
#include <QLabel>

class ImageViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageViewerDialog(QWidget* parent = nullptr);
    ~ImageViewerDialog();

    void open(ImageViewerController* controller);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QGraphicsScene* scene;
    QGraphicsView* graphicsView;
    QGraphicsPixmapItem* pixmapItem;
    QLabel* infoLabel;

    ImageViewerController* controller = nullptr;
    bool isPanning = false;
    QPoint lastPanPoint;

    void applyTransform(const ViewerTransform& xform);
    void fitToWindow(const ViewerTransform& xform);
    void updateInfoLabel();
    void updateCursor();

    void onImageReady(QPixmap pixmap);
    void onImageLoading();
    void onIndexChanged(int index, int total);
    void onPicInfoChanged(const PicInfo* info, const Metadata* metadata);
    void onTransformChanged(const ViewerTransform& xform);
};
