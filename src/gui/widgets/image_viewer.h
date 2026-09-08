#pragma once

#include "gui/controllers/image_viewer_controller.h"
#include <QDialog>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QCompleter>

class TagPill : public QWidget {
    Q_OBJECT
public:
    TagPill(const QString& text, uint32_t tagId, int category, bool hasParents = false, bool hasChildren = false, QWidget* parent = nullptr);
    uint32_t tagId() const { return m_tagId; }
    int category() const { return m_category; }

signals:
    void removeRequested(uint32_t tagId);

private:
    uint32_t m_tagId;
    int m_category;
};

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

    QWidget* metadataPanel;
    QLabel* fileInfoLabel;
    QLabel* sourceInfoLabel;
    QWidget* tagsContainer;
    QLineEdit* tagInput;
    QCompleter* tagCompleter;
    QLabel* socialLabel;
    QLabel* datesLabel;
    bool metadataPanelVisible = true;

    ImageViewerController* controller = nullptr;
    bool isPanning = false;
    QPoint lastPanPoint;

    void applyTransform(const ViewerTransform& xform);
    void fitToWindow(const ViewerTransform& xform);
    void updateInfoLabel();
    void updateCursor();

    void buildMetadataPanel();
    void populateMetadataPanel();
    void rebuildTagPills();
    void toggleMetadataPanel();
    void onTagInputReturnPressed();

    void onImageReady(QPixmap pixmap);
    void onImageLoading();
    void onIndexChanged(int index, int total);
    void onPicInfoChanged(const PicInfo* info, const Metadata* metadata);
    void onTransformChanged(const ViewerTransform& xform);
    void onTagsChanged();
};
