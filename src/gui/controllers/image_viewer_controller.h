#pragma once

#include "service/model.h"
#include <QObject>
#include <QPixmap>
#include <vector>

class ImageLoader;
enum class LoadType;
class PicDatabase;

struct ViewerTransform {
    double zoomLevel = 1.0;
    bool fitMode = true;
    int rotationAngle = 0;
    bool hFlip = false;
    bool vFlip = false;
    bool animPaused = false;
};

struct TagDisplay {
    uint32_t tagId = 0;
    std::string name;
    int category = 0;
    int source = 0;
    bool hasParents = false;
    bool hasChildren = false;
};

class ImageViewerController : public QObject {
    Q_OBJECT
public:
    explicit ImageViewerController(QObject* parent = nullptr);

    void setup(const DisplayItems* items, int startIndex, ImageLoader& loader, PicDatabase& db);
    void reset();

    const PicInfo* currentPicInfo() const;
    const Metadata* currentMetadata() const;
    int currentIndex() const { return index; }
    int totalCount() const;
    const ViewerTransform& transform() const { return xform; }

    std::vector<TagDisplay> currentTagDisplays() const;

    void addTag(const std::string& tagName);
    void removeTag(uint32_t tagId);
    std::vector<std::string> getTagSuggestions(const std::string& prefix) const;

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
    void tagsChanged();

private:
    const DisplayItems* items = nullptr;
    int index = 0;
    ImageLoader* imageLoader = nullptr;
    PicDatabase* database = nullptr;
    ViewerTransform xform;

    void preloadAdjacent();
    void emitIndexChanged();
    void resetTransform();
};
