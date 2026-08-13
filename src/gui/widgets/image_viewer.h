#pragma once

#include "gui/controllers/image_viewer_controller.h"
#include <QDialog>
#include <QGraphicsView>
#include <QLabel>
#include <QScrollArea>

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
    // Image view
    QGraphicsScene* scene;
    QGraphicsView* graphicsView;
    QGraphicsPixmapItem* pixmapItem;
    QLabel* infoLabel;

    // Metadata panel
    QWidget* metadataPanel;
    QScrollArea* metadataScroll;
    QLabel* fileInfoLabel;
    QLabel* sourceInfoLabel;
    QLabel* tagsLabel;
    QLabel* socialLabel;
    QLabel* datesLabel;
    bool metadataPanelVisible = true;

    // Controller
    ImageViewerController* controller = nullptr;
    bool isPanning = false;
    QPoint lastPanPoint;

    // Transform
    void applyTransform(const ViewerTransform& xform);
    void fitToWindow(const ViewerTransform& xform);
    void updateInfoLabel();
    void updateCursor();

    // Metadata panel
    void buildMetadataPanel();
    void populateMetadataPanel();
    void toggleMetadataPanel();

    // Slots
    void onImageReady(QPixmap pixmap);
    void onImageLoading();
    void onIndexChanged(int index, int total);
    void onPicInfoChanged(const PicInfo* info, const Metadata* metadata);
    void onTransformChanged(const ViewerTransform& xform);
};
