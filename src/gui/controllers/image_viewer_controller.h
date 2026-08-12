#pragma once

#include "service/model.h"
#include <QObject>
#include <QPixmap>

class ImageLoader;
enum class LoadType;

struct ViewerTransform {
    double zoomLevel = 1.0;
    bool fitMode = true;
    int rotationAngle = 0;
    bool hFlip = false;
    bool vFlip = false;
    bool animPaused = false;
};

class ImageViewerController : public QObject {
    Q_OBJECT
public:
    explicit ImageViewerController(QObject* parent = nullptr);

    void setup(const DisplayItems* items, int startIndex, ImageLoader& loader);
    void reset();

    const PicInfo* currentPicInfo() const;
    const Metadata* currentMetadata() const;
    int currentIndex() const { return index; }
    int totalCount() const;
    const ViewerTransform& transform() const { return xform; }

    void navigateTo(int index);
    void navigateNext();
    void navigatePrev();
    void navigateFirst();
    void navigateLast();

    void zoomIn();
    void zoomOut();
    void setZoomLevel(double level);
    void setOriginalSize();
    void toggleFitMode();

    void rotateCW();
    void rotateCCW();
    void flipHorizontal();
    void flipVertical();
    void toggleAnimPause();

    void requestCurrentImage();

    void handleImageLoaded(uint64_t picId, LoadType loadType);

signals:
    void imageReady(QPixmap pixmap);
    void imageLoading();
    void indexChanged(int index, int total);
    void picInfoChanged(const PicInfo* info, const Metadata* metadata);
    void transformChanged(const ViewerTransform& xform);

private:
    const DisplayItems* items = nullptr;
    int index = 0;
    ImageLoader* imageLoader = nullptr;
    ViewerTransform xform;

    void preloadAdjacent();
    void emitIndexChanged();
    void resetTransform();
};
