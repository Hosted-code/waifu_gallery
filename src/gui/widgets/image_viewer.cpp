#include "image_viewer.h"
#include <QApplication>
#include <QCompleter>
#include <QGraphicsPixmapItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QStringListModel>
#include <QVBoxLayout>

namespace {

const QString SECTION_STYLE = "QLabel { color: #8B5CF6; font-size: 12px; font-weight: bold; padding: 6px 0 2px 0; }";
const QString VALUE_STYLE = "QLabel { color: #9CA3AF; font-size: 11px; padding: 1px 0; }";
const QString PANEL_BG = "#1D2027";

QString formatFileSize(uint32_t bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QString imageFormatStr(ImageFormat fmt) {
    switch (fmt) {
    case ImageFormat::JPG: return "JPG";
    case ImageFormat::PNG: return "PNG";
    case ImageFormat::GIF: return "GIF";
    case ImageFormat::WebP: return "WebP";
    default: return "Unknown";
    }
}

QString platformTypeStr(PlatformType p) {
    switch (p) {
    case PlatformType::Pixiv: return "Pixiv";
    case PlatformType::Twitter: return "Twitter";
    default: return "Unknown";
    }
}

QString restrictTypeStr(RestrictType r) {
    switch (r) {
    case RestrictType::AllAges: return "All Ages";
    case RestrictType::Sensitive: return "Sensitive";
    case RestrictType::Questionable: return "Questionable";
    case RestrictType::R18: return "R18";
    case RestrictType::R18G: return "R18G";
    default: return "Unknown";
    }
}

QString aiTypeStr(AIType a) {
    switch (a) {
    case AIType::NotAI: return "Not AI";
    case AIType::AI: return "AI";
    default: return "Unknown";
    }
}

const QString PILL_STYLE_CHAR = "background: #3D1F3A; border: 1px solid #EC4899; border-radius: 3px; color: #EC4899;";
const QString PILL_STYLE_ATTR = "background: #1A2E3D; border: 1px solid #38BDF8; border-radius: 3px; color: #38BDF8;";
const QString PILL_STYLE_PLAT = "background: #2A1F3D; border: 1px solid #8B5CF6; border-radius: 3px; color: #8B5CF6;";

} // namespace

TagPill::TagPill(const QString& text, uint32_t tagId, bool isPlatformTag, QWidget* parent)
    : QWidget(parent), m_tagId(tagId), m_isPlatformTag(isPlatformTag) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 1, 1, 1);
    layout->setSpacing(2);

    auto* nameLabel = new QLabel(text, this);
    nameLabel->setStyleSheet("border: none; background: transparent;");
    layout->addWidget(nameLabel);

    auto* removeBtn = new QPushButton("x", this);
    removeBtn->setFixedSize(14, 14);
    removeBtn->setStyleSheet("QPushButton { border: none; background: transparent; color: #666; font-size: 10px; }"
                             "QPushButton:hover { color: #F87171; }");
    connect(removeBtn, &QPushButton::clicked, this, [this]() { emit removeRequested(m_tagId, m_isPlatformTag); });
    layout->addWidget(removeBtn);

    setStyleSheet(isPlatformTag ? PILL_STYLE_PLAT : PILL_STYLE_ATTR);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

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
    graphicsView->setStyleSheet("QGraphicsView { background: #15171C; border: none; }");

    infoLabel = new QLabel(this);
    infoLabel->setStyleSheet("QLabel { color: #9CA3AF; font-size: 12px; padding: 4px 8px; }");
    infoLabel->setAlignment(Qt::AlignCenter);

    buildMetadataPanel();

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(graphicsView, 1);
    leftLayout->addWidget(infoLabel);

    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(metadataPanel);

    graphicsView->viewport()->installEventFilter(this);

    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        resize(screenGeometry.width() * 0.85, screenGeometry.height() * 0.85);
    }

    updateCursor();
}

ImageViewerDialog::~ImageViewerDialog() = default;

void ImageViewerDialog::buildMetadataPanel() {
    metadataPanel = new QWidget(this);
    metadataPanel->setFixedWidth(280);
    metadataPanel->setStyleSheet(QString("QWidget { background: %1; }").arg(PANEL_BG));

    auto* panelLayout = new QVBoxLayout(metadataPanel);
    panelLayout->setContentsMargins(12, 8, 12, 8);
    panelLayout->setSpacing(2);

    auto* fileHeader = new QLabel("FILE", metadataPanel);
    fileHeader->setStyleSheet(SECTION_STYLE);
    fileInfoLabel = new QLabel(metadataPanel);
    fileInfoLabel->setStyleSheet(VALUE_STYLE);
    fileInfoLabel->setWordWrap(true);
    fileInfoLabel->setTextFormat(Qt::PlainText);

    auto* sourceHeader = new QLabel("SOURCE", metadataPanel);
    sourceHeader->setStyleSheet(SECTION_STYLE);
    sourceInfoLabel = new QLabel(metadataPanel);
    sourceInfoLabel->setStyleSheet(VALUE_STYLE);
    sourceInfoLabel->setWordWrap(true);
    sourceInfoLabel->setTextFormat(Qt::PlainText);

    auto* tagsHeader = new QLabel("TAGS", metadataPanel);
    tagsHeader->setStyleSheet(SECTION_STYLE);
    tagsContainer = new QWidget(metadataPanel);
    tagsContainer->setStyleSheet("QWidget { background: transparent; }");
    auto* tagsLayout = new QVBoxLayout(tagsContainer);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->setSpacing(2);
    tagsLayout->setAlignment(Qt::AlignTop);

    tagInput = new QLineEdit(metadataPanel);
    tagInput->setPlaceholderText("Add tag...");
    tagInput->setStyleSheet("QLineEdit { background: #252933; border: 1px solid #30343D; border-radius: 3px;"
                            " color: #F5F7FA; padding: 3px 6px; font-size: 11px; }"
                            "QLineEdit::placeholder { color: #555; }");
    tagCompleter = new QCompleter(metadataPanel);
    tagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    tagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    tagCompleter->setMaxVisibleItems(10);
    tagInput->setCompleter(tagCompleter);

    auto* socialHeader = new QLabel("SOCIAL", metadataPanel);
    socialHeader->setStyleSheet(SECTION_STYLE);
    socialLabel = new QLabel(metadataPanel);
    socialLabel->setStyleSheet(VALUE_STYLE);
    socialLabel->setWordWrap(true);
    socialLabel->setTextFormat(Qt::PlainText);

    auto* datesHeader = new QLabel("DATES", metadataPanel);
    datesHeader->setStyleSheet(SECTION_STYLE);
    datesLabel = new QLabel(metadataPanel);
    datesLabel->setStyleSheet(VALUE_STYLE);
    datesLabel->setWordWrap(true);
    datesLabel->setTextFormat(Qt::PlainText);

    panelLayout->addWidget(fileHeader);
    panelLayout->addWidget(fileInfoLabel);
    panelLayout->addSpacing(6);
    panelLayout->addWidget(sourceHeader);
    panelLayout->addWidget(sourceInfoLabel);
    panelLayout->addSpacing(6);
    panelLayout->addWidget(tagsHeader);
    panelLayout->addWidget(tagsContainer, 1);
    panelLayout->addWidget(tagInput);
    panelLayout->addSpacing(6);
    panelLayout->addWidget(socialHeader);
    panelLayout->addWidget(socialLabel);
    panelLayout->addSpacing(6);
    panelLayout->addWidget(datesHeader);
    panelLayout->addWidget(datesLabel);
    panelLayout->addStretch();

    connect(tagInput, &QLineEdit::returnPressed, this, &ImageViewerDialog::onTagInputReturnPressed);
    connect(tagInput, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!controller || text.isEmpty()) return;
        auto suggestions = controller->getTagSuggestions(text.toStdString());
        QStringList model;
        for (const auto& s : suggestions) model << QString::fromStdString(s);
        tagCompleter->setModel(new QStringListModel(model, tagCompleter));
        tagCompleter->setCompletionPrefix(text);
    });
}

void ImageViewerDialog::populateMetadataPanel() {
    if (!controller) return;

    const PicInfo* info = controller->currentPicInfo();
    const Metadata* meta = controller->currentMetadata();

    if (info) {
        QStringList lines;
        lines << QString("%1 x %2").arg(info->width).arg(info->height);
        lines << QString("%1 | %2").arg(imageFormatStr(info->fileType)).arg(formatFileSize(info->size));
        if (!info->filePaths.empty()) {
            lines << QString::fromStdString(info->filePaths[0].string());
        }
        lines << QString("Restrict: %1").arg(restrictTypeStr(info->restrictType));
        lines << QString("AI: %1").arg(aiTypeStr(info->aiType));
        fileInfoLabel->setText(lines.join("\n"));
    } else {
        fileInfoLabel->setText("N/A");
    }

    if (meta) {
        QStringList lines;
        lines << QString("Platform: %1").arg(platformTypeStr(meta->platformType));
        lines << QString("ID: %1").arg(meta->id);
        if (!meta->authorName.empty()) lines << QString("Author: %1").arg(QString::fromStdString(meta->authorName));
        if (!meta->authorNick.empty()) lines << QString("Nick: %1").arg(QString::fromStdString(meta->authorNick));
        if (!meta->title.empty()) lines << QString("Title: %1").arg(QString::fromStdString(meta->title));
        if (!meta->description.empty()) {
            QString desc = QString::fromStdString(meta->description);
            if (desc.length() > 200) desc = desc.left(200) + "...";
            lines << desc;
        }
        sourceInfoLabel->setText(lines.join("\n"));
    } else {
        sourceInfoLabel->setText("No source metadata");
    }

    rebuildTagPills();

    if (meta) {
        QStringList lines;
        if (meta->viewCount > 0) lines << QString("Views: %1").arg(meta->viewCount);
        if (meta->likeCount > 0) lines << QString("Likes: %1").arg(meta->likeCount);
        if (meta->bookmarkCount > 0) lines << QString("Bookmarks: %1").arg(meta->bookmarkCount);
        if (meta->replyCount > 0) lines << QString("Replies: %1").arg(meta->replyCount);
        if (meta->forwardCount > 0) lines << QString("Forwards: %1").arg(meta->forwardCount);
        if (meta->quoteCount > 0) lines << QString("Quotes: %1").arg(meta->quoteCount);
        socialLabel->setText(lines.empty() ? "No social data" : lines.join("\n"));
    } else {
        socialLabel->setText("No social data");
    }

    QStringList dateLines;
    if (info) {
        if (!info->downloadTime.empty()) dateLines << QString("Download: %1").arg(QString::fromStdString(info->downloadTime));
        if (!info->editTime.empty()) dateLines << QString("Modified: %1").arg(QString::fromStdString(info->editTime));
    }
    if (meta && !meta->date.empty()) dateLines << QString("Published: %1").arg(QString::fromStdString(meta->date));
    datesLabel->setText(dateLines.empty() ? "No date info" : dateLines.join("\n"));
}

void ImageViewerDialog::rebuildTagPills() {
    if (!controller) return;

    auto* tagsLayout = qobject_cast<QVBoxLayout*>(tagsContainer->layout());
    QLayoutItem* child;
    while ((child = tagsLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    auto tagDisplays = controller->currentTagDisplays();
    if (tagDisplays.empty()) {
        auto* emptyLabel = new QLabel("No tags", tagsContainer);
        emptyLabel->setStyleSheet(VALUE_STYLE);
        tagsLayout->addWidget(emptyLabel);
    } else {
        for (const auto& td : tagDisplays) {
            auto* pill = new TagPill(QString::fromStdString(td.name), td.tagId, td.isPlatformTag, tagsContainer);
            if (td.isCharacter) pill->setStyleSheet(PILL_STYLE_CHAR);
            connect(pill, &TagPill::removeRequested, this, [this](uint32_t tagId, bool isPlatformTag) {
                if (!isPlatformTag && controller) controller->removeTag(tagId);
            });
            tagsLayout->addWidget(pill);
        }
    }
}

void ImageViewerDialog::onTagInputReturnPressed() {
    if (!controller) return;
    QString text = tagInput->text().trimmed();
    if (text.isEmpty()) return;

    controller->addTag(text.toStdString());
    tagInput->clear();
}

void ImageViewerDialog::toggleMetadataPanel() {
    metadataPanelVisible = !metadataPanelVisible;
    metadataPanel->setVisible(metadataPanelVisible);
    if (controller && controller->transform().fitMode) {
        fitToWindow(controller->transform());
    }
}

void ImageViewerDialog::open(ImageViewerController* ctrl) {
    if (controller) disconnect(controller, nullptr, this, nullptr);
    controller = ctrl;

    connect(controller, &ImageViewerController::imageReady, this, &ImageViewerDialog::onImageReady);
    connect(controller, &ImageViewerController::imageLoading, this, &ImageViewerDialog::onImageLoading);
    connect(controller, &ImageViewerController::indexChanged, this, &ImageViewerDialog::onIndexChanged);
    connect(controller, &ImageViewerController::picInfoChanged, this, &ImageViewerDialog::onPicInfoChanged);
    connect(controller, &ImageViewerController::transformChanged, this, &ImageViewerDialog::onTransformChanged);
    connect(controller, &ImageViewerController::tagsChanged, this, &ImageViewerDialog::onTagsChanged);

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
            wheelEvent->angleDelta().y() > 0 ? controller->zoomIn() : controller->zoomOut();
        } else {
            wheelEvent->angleDelta().y() > 0 ? controller->navigatePrev() : controller->navigateNext();
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
    Q_UNUSED(index); Q_UNUSED(total);
    updateInfoLabel();
}

void ImageViewerDialog::onPicInfoChanged(const PicInfo* info, const Metadata* metadata) {
    Q_UNUSED(info); Q_UNUSED(metadata);
    populateMetadataPanel();
}

void ImageViewerDialog::onTransformChanged(const ViewerTransform& xform) {
    xform.fitMode ? fitToWindow(xform) : applyTransform(xform);
    updateInfoLabel();
}

void ImageViewerDialog::onTagsChanged() {
    rebuildTagPills();
}

void ImageViewerDialog::fitToWindow(const ViewerTransform& xform) {
    if (pixmapItem->pixmap().isNull()) return;
    QSizeF pixmapSize = pixmapItem->pixmap().size();
    if (xform.rotationAngle == 90 || xform.rotationAngle == 270) std::swap(pixmapSize.rwidth(), pixmapSize.rheight());
    QSizeF viewSize = QSizeF(graphicsView->viewport()->size());
    double fitScale = std::min(viewSize.width() / pixmapSize.width(), viewSize.height() / pixmapSize.height());
    graphicsView->resetTransform();
    graphicsView->scale(fitScale, fitScale);
    if (xform.hFlip) graphicsView->scale(-1, 1);
    if (xform.vFlip) graphicsView->scale(1, -1);
    if (xform.rotationAngle != 0) graphicsView->rotate(xform.rotationAngle);
    graphicsView->centerOn(pixmapItem);
}

void ImageViewerDialog::applyTransform(const ViewerTransform& xform) {
    graphicsView->resetTransform();
    graphicsView->scale(xform.zoomLevel, xform.zoomLevel);
    if (xform.hFlip) graphicsView->scale(-1, 1);
    if (xform.vFlip) graphicsView->scale(1, -1);
    if (xform.rotationAngle != 0) graphicsView->rotate(xform.rotationAngle);
    graphicsView->centerOn(pixmapItem);
}

void ImageViewerDialog::updateInfoLabel() {
    if (!controller) return;
    const ViewerTransform& xform = controller->transform();
    QStringList parts;
    parts << QString("%1 / %2").arg(controller->currentIndex() + 1).arg(controller->totalCount());
    const PicInfo* info = controller->currentPicInfo();
    if (info) parts << QString("%1x%2").arg(info->width).arg(info->height);
    parts << QString("%1%").arg(qRound(xform.zoomLevel * 100));
    if (xform.rotationAngle != 0) parts << QString("%1\u00B0").arg(xform.rotationAngle);
    if (xform.hFlip || xform.vFlip) parts << (xform.hFlip && xform.vFlip ? "HV" : xform.hFlip ? "H" : "V");
    if (xform.animPaused) parts << QString::fromUtf8("\u23F8");
    infoLabel->setText(parts.join("  |  "));
}

void ImageViewerDialog::updateCursor() {
    graphicsView->setCursor(isPanning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
}

void ImageViewerDialog::keyPressEvent(QKeyEvent* event) {
    if (!controller) { QDialog::keyPressEvent(event); return; }
    if (tagInput->hasFocus()) { QDialog::keyPressEvent(event); return; }

    switch (event->key()) {
    case Qt::Key_Escape: hide(); break;
    case Qt::Key_Left: case Qt::Key_PageUp: case Qt::Key_Backspace: controller->navigatePrev(); break;
    case Qt::Key_Right: case Qt::Key_PageDown: case Qt::Key_Space: controller->navigateNext(); break;
    case Qt::Key_Home: controller->navigateFirst(); break;
    case Qt::Key_End: controller->navigateLast(); break;
    case Qt::Key_F: controller->toggleFitMode(); break;
    case Qt::Key_1: controller->setOriginalSize(); break;
    case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_F11: isFullScreen() ? showNormal() : showFullScreen(); break;
    case Qt::Key_Plus: case Qt::Key_Equal: controller->zoomIn(); break;
    case Qt::Key_Minus: controller->zoomOut(); break;
    case Qt::Key_H: controller->flipHorizontal(); break;
    case Qt::Key_V: controller->flipVertical(); break;
    case Qt::Key_R: controller->rotateCW(); break;
    case Qt::Key_L: controller->rotateCCW(); break;
    case Qt::Key_P: controller->toggleAnimPause(); break;
    case Qt::Key_I: toggleMetadataPanel(); break;
    default: QDialog::keyPressEvent(event);
    }
}

void ImageViewerDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (controller && controller->transform().fitMode) fitToWindow(controller->transform());
}
