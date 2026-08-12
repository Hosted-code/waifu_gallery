#include "image_viewer_controller.h"
#include "image_loader.h"

constexpr int PRELOAD_COUNT = 2;
constexpr double MIN_ZOOM = 0.05;
constexpr double MAX_ZOOM = 32.0;
constexpr double ZOOM_STEP = 1.25;

ImageViewerController::ImageViewerController(QObject* parent) : QObject(parent) {}

void ImageViewerController::setup(const DisplayItems* items, int startIndex, ImageLoader& loader) {
    this->items = items;
    this->imageLoader = &loader;
    this->index = startIndex;
    resetTransform();
    requestCurrentImage();
    preloadAdjacent();
    emitIndexChanged();
}

void ImageViewerController::reset() {
    items = nullptr;
    imageLoader = nullptr;
    index = 0;
    resetTransform();
}

const PicInfo* ImageViewerController::currentPicInfo() const {
    if (!items || index < 0 || index >= (int)items->picItems.size()) return nullptr;
    return &items->picItems[index].info;
}

const Metadata* ImageViewerController::currentMetadata() const {
    if (!items || index < 0 || index >= (int)items->picItems.size()) return nullptr;
    const PicItem& picItem = items->picItems[index];
    if (picItem.metadataCount == 0) return nullptr;
    return &items->metadataItems[picItem.metadataStartIndex].metadata;
}

int ImageViewerController::totalCount() const {
    if (!items) return 0;
    return (int)items->picItems.size();
}

void ImageViewerController::navigateTo(int newIndex) {
    if (!items || newIndex < 0 || newIndex >= (int)items->picItems.size()) return;
    if (newIndex == index) return;
    index = newIndex;
    resetTransform();
    requestCurrentImage();
    preloadAdjacent();
    emitIndexChanged();
}

void ImageViewerController::navigateNext() { navigateTo(index + 1); }
void ImageViewerController::navigatePrev() { navigateTo(index - 1); }
void ImageViewerController::navigateFirst() { navigateTo(0); }
void ImageViewerController::navigateLast() {
    if (items) navigateTo((int)items->picItems.size() - 1);
}

void ImageViewerController::zoomIn() {
    setZoomLevel(xform.zoomLevel * ZOOM_STEP);
}

void ImageViewerController::zoomOut() {
    setZoomLevel(xform.zoomLevel / ZOOM_STEP);
}

void ImageViewerController::setZoomLevel(double level) {
    level = qBound(MIN_ZOOM, level, MAX_ZOOM);
    xform.zoomLevel = level;
    xform.fitMode = false;
    emit transformChanged(xform);
}

void ImageViewerController::setOriginalSize() {
    xform.zoomLevel = 1.0;
    xform.fitMode = false;
    emit transformChanged(xform);
}

void ImageViewerController::toggleFitMode() {
    if (xform.fitMode) {
        setOriginalSize();
    } else {
        xform.fitMode = true;
        emit transformChanged(xform);
    }
}

void ImageViewerController::rotateCW() {
    xform.rotationAngle = (xform.rotationAngle + 90) % 360;
    emit transformChanged(xform);
}

void ImageViewerController::rotateCCW() {
    xform.rotationAngle = (xform.rotationAngle - 90 + 360) % 360;
    emit transformChanged(xform);
}

void ImageViewerController::flipHorizontal() {
    xform.hFlip = !xform.hFlip;
    emit transformChanged(xform);
}

void ImageViewerController::flipVertical() {
    xform.vFlip = !xform.vFlip;
    emit transformChanged(xform);
}

void ImageViewerController::toggleAnimPause() {
    xform.animPaused = !xform.animPaused;
    emit transformChanged(xform);
}

void ImageViewerController::requestCurrentImage() {
    if (!items || !imageLoader) return;
    if (index < 0 || index >= (int)items->picItems.size()) return;

    const PicInfo* info = currentPicInfo();
    if (!info) return;

    if (auto* img = imageLoader->getImage(*info, LoadType::FullSize)) {
        emit imageReady(QPixmap::fromImage(*img));
    } else {
        emit imageLoading();
    }

    emit picInfoChanged(currentPicInfo(), currentMetadata());
}

void ImageViewerController::handleImageLoaded(uint64_t picId, LoadType loadType) {
    if (loadType != LoadType::FullSize || !items) return;
    if (index < 0 || index >= (int)items->picItems.size()) return;
    if (items->picItems[index].info.id != picId) return;

    if (auto* img = imageLoader->getImage(picId, LoadType::FullSize)) {
        emit imageReady(QPixmap::fromImage(*img));
    }
}

void ImageViewerController::preloadAdjacent() {
    if (!items || !imageLoader) return;
    const auto& pics = items->picItems;
    for (int offset = 1; offset <= PRELOAD_COUNT; ++offset) {
        if (index - offset >= 0) imageLoader->getImage(pics[index - offset].info, LoadType::FullSize);
        if (index + offset < (int)pics.size()) imageLoader->getImage(pics[index + offset].info, LoadType::FullSize);
    }
}

void ImageViewerController::emitIndexChanged() {
    emit indexChanged(index, totalCount());
}

void ImageViewerController::resetTransform() {
    xform = ViewerTransform{};
}
