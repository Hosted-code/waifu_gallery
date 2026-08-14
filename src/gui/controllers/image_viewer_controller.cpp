#include "image_viewer_controller.h"
#include "image_loader.h"
#include "service/database.h"
#include "utils/logger.h"
#include <QString>

constexpr int PRELOAD_COUNT = 2;
constexpr double MIN_ZOOM = 0.05;
constexpr double MAX_ZOOM = 32.0;
constexpr double ZOOM_STEP = 1.25;

ImageViewerController::ImageViewerController(QObject* parent) : QObject(parent) {}

void ImageViewerController::setup(const DisplayItems* items, int startIndex, ImageLoader& loader, PicDatabase& db) {
    this->items = items;
    this->imageLoader = &loader;
    this->database = &db;
    this->index = startIndex;
    resetTransform();
    requestCurrentImage();
    preloadAdjacent();
    emitIndexChanged();
}

void ImageViewerController::reset() {
    items = nullptr;
    imageLoader = nullptr;
    database = nullptr;
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

std::vector<TagDisplay> ImageViewerController::currentTagDisplays() const {
    std::vector<TagDisplay> result;
    const PicInfo* info = currentPicInfo();
    if (!info) return result;

    auto& cache = DbCache::getInstance();

    // AI tags from PicInfo::tags
    result.reserve(info->tags.size());
    for (const PicTag& picTag : info->tags) {
        TagStr tagStr = cache.getStringTag(picTag.tagId);
        if (!tagStr.tag.empty()) {
            result.push_back(TagDisplay{picTag.tagId, tagStr.tag, tagStr.isCharacter});
        }
    }

    // Platform tags from Metadata::tagIds
    const Metadata* meta = currentMetadata();
    if (meta) {
        for (uint32_t tagId : meta->tagIds) {
            PlatformTagStr ptStr = cache.getPlatformStringTag(tagId);
            if (!ptStr.tag.empty()) {
                auto it = std::find_if(result.begin(), result.end(),
                    [&](const TagDisplay& td) { return td.name == ptStr.tag; });
                if (it == result.end()) {
                    result.push_back(TagDisplay{tagId, ptStr.tag, false});
                }
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const TagDisplay& a, const TagDisplay& b) {
        if (a.isCharacter != b.isCharacter) return a.isCharacter;
        return a.name < b.name;
    });

    return result;
}

void ImageViewerController::addTag(const std::string& tagName) {
    if (!database || !currentPicInfo()) return;

    auto currentTags = currentTagDisplays();
    for (const auto& td : currentTags) {
        if (td.name == tagName) return;
    }

    uint32_t tagId;
    auto existingId = database->getAITagIdByTagText(tagName);
    if (existingId.has_value()) {
        tagId = existingId.value();
    } else {
        tagId = database->addAITag(tagName, false);
        if (tagId == 0) return;
    }

    uint64_t picId = currentPicInfo()->id;
    if (database->addTagToPicture(picId, tagId, 1.0f)) {
        PicInfo* mutableInfo = const_cast<PicInfo*>(currentPicInfo());
        PicTag newTag{tagId, 1.0f};
        mutableInfo->tags.push_back(newTag);
        database->syncMetadataFile(*currentPicInfo(), currentMetadata(), tagName, true);
        emit tagsChanged();
    }
}

void ImageViewerController::removeTag(uint32_t tagId) {
    if (!database || !currentPicInfo()) return;

    auto& cache = DbCache::getInstance();
    std::string tagName;

    if (database->isAITag(tagId)) {
        uint64_t picId = currentPicInfo()->id;
        if (!database->removeTagFromPicture(picId, tagId)) return;
        TagStr tagStr = cache.getStringTag(tagId);
        tagName = tagStr.tag;
        PicInfo* mutableInfo = const_cast<PicInfo*>(currentPicInfo());
        auto& tags = mutableInfo->tags;
        tags.erase(std::remove_if(tags.begin(), tags.end(),
            [tagId](const PicTag& t) { return t.tagId == tagId; }), tags.end());
    } else if (database->isPlatformTag(tagId)) {
        const Metadata* meta = currentMetadata();
        if (!meta) return;
        PlatformTagStr ptStr = cache.getPlatformStringTag(tagId);
        tagName = ptStr.tag;
        if (!database->removePlatformTagFromMetadata(meta->platformType, meta->id, tagId)) return;
        Metadata* mutableMeta = const_cast<Metadata*>(meta);
        auto& tagIds = mutableMeta->tagIds;
        tagIds.erase(std::remove(tagIds.begin(), tagIds.end(), tagId), tagIds.end());
    } else {
        return;
    }

    if (!tagName.empty()) {
        database->syncMetadataFile(*currentPicInfo(), currentMetadata(), tagName, false);
    }
    emit tagsChanged();
}

std::vector<std::string> ImageViewerController::getTagSuggestions(const std::string& prefix) const {
    std::vector<std::string> result;
    if (!database || prefix.empty()) return result;

    QString qPrefix = QString::fromUtf8(prefix.c_str());
    const auto& allTags = database->getAllAITags();
    for (const auto& tag : allTags) {
        if (QString::fromUtf8(tag.tag.c_str()).startsWith(qPrefix, Qt::CaseInsensitive)) {
            result.push_back(tag.tag);
        }
    }

    std::sort(result.begin(), result.end());
    if (result.size() > 20) result.resize(20);
    return result;
}
