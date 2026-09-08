/*
 * Waifu Gallery - A anime illustration gallery application.
 * Copyright (C) 2025 R4nd5tr <r4nd5tr@outlook.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include "main_window.h"
#include "controllers/utils.h"
#include "service/database.h"
#include "tag_picker_dialog.h"
#include "ui_main_window.h"
#include "utils/settings.h"
#include <QFileDialog>
#include <QScrollBar>
#include <QString>

const QEvent::Type ImportProgressReportEvent::EventType = static_cast<QEvent::Type>(QEvent::registerEventType());
const QEvent::Type TaggingProgressReportEvent::EventType = static_cast<QEvent::Type>(QEvent::registerEventType());

// MainWindow implementation

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), displayController(ui, &imageLoader) {
    ui->setupUi(this);
    displayController.setup();

    Settings::loadSettings();
    resize(Settings::windowWidth, Settings::windowHeight);
    if (Settings::autoImportOnStartup) handleImportExistingDirectoriesAction();

    initInterface();
    initWorkerThreads();
    connectSignalSlots();
    loadTags();
    displayTags();

    initTagger();
}
MainWindow::~MainWindow() {
    delete ui;
    searchWorkerThread->quit();
    searchWorkerThread->wait();
    Settings::saveSettings();
}

// Initialize functions

void MainWindow::initInterface() {
    ui->selectedTagScrollArea->hide();
    ui->progressWidget->hide();
    fillComboBox();
}
void MainWindow::fillComboBox() {
    ui->searchPlatformComboBox->addItem("选择平台");
    ui->searchPlatformComboBox->addItem("Pixiv");
    ui->searchPlatformComboBox->addItem("Twitter");
    ui->searchPlatformComboBox->setCurrentIndex(0);

    ui->searchComboBox->addItem("选择搜索字段");
    ui->searchComboBox->addItem("平台ID");
    ui->searchComboBox->addItem("作者ID");
    ui->searchComboBox->addItem("作者名");
    ui->searchComboBox->addItem("作者别名");
    ui->searchComboBox->addItem("标题");
    ui->searchComboBox->setCurrentIndex(0);

    ui->sortComboBox->addItem("无");
    ui->sortComboBox->addItem("图片ID");
    ui->sortComboBox->addItem("下载日期");
    ui->sortComboBox->addItem("编辑日期");
    ui->sortComboBox->addItem("大小");
    ui->sortComboBox->addItem("文件名");
    ui->sortComboBox->addItem("宽度");
    ui->sortComboBox->addItem("高度");
    ui->sortComboBox->setCurrentIndex(0);

    ui->orderComboBox->addItem("升序");
    ui->orderComboBox->addItem("降序");
    ui->orderComboBox->setCurrentIndex(0);
}
void MainWindow::initWorkerThreads() {
    searchWorkerThread = new QThread(this);
    DatabaseWorker* searchWorker = new DatabaseWorker();
    searchWorker->moveToThread(searchWorkerThread);
    connect(this, &MainWindow::searchPics, searchWorker, &DatabaseWorker::searchPics);
    connect(searchWorker, &DatabaseWorker::searchComplete, this, &MainWindow::handleSearchResults);
    connect(searchWorkerThread, &QThread::finished, searchWorker, &QObject::deleteLater);
    searchWorkerThread->start();
}
void MainWindow::connectSignalSlots() {
    // debounce timers
    resolutionTimer.setSingleShot(true);
    ratioSortTimer.setSingleShot(true);
    tagClickTimer.setSingleShot(true);
    tagSearchTimer.setSingleShot(true);

    // filter checkboxes
    connect(ui->unknowPlatformCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowUnknowPlatform);
    connect(ui->pixivPlatformCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowPixiv);
    connect(ui->twitterPlatformCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowTwitter);
    connect(ui->jpgCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowJPG);
    connect(ui->pngCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowPNG);
    connect(ui->gifCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowGIF);
    connect(ui->webpCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowWEBP);
    connect(ui->unknowRestrictCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowUnknowRestrict);
    connect(ui->allAgeCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowAllAge);
    connect(ui->sensitiveCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowSensitive);
    connect(ui->questionableCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowQuestionable);
    connect(ui->r18CheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowR18);
    connect(ui->r18gCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowR18g);
    connect(ui->unknowAICheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowUnknowAI);
    connect(ui->aiCheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowAI);
    connect(ui->noAICheckBox, &QCheckBox::toggled, this, &MainWindow::updateShowNonAI);

    // resolution filters
    connect(ui->maxHeightEdit, &QLineEdit::textChanged, this, &MainWindow::updateMaxHeight);
    connect(ui->minHeightEdit, &QLineEdit::textChanged, this, &MainWindow::updateMinHeight);
    connect(ui->maxWidthEdit, &QLineEdit::textChanged, this, &MainWindow::updateMaxWidth);
    connect(ui->minWidthEdit, &QLineEdit::textChanged, this, &MainWindow::updateMinWidth);
    connect(ui->maxHeightEdit, &QLineEdit::textChanged, this, [this]() { resolutionTimer.start(DEBOUNCE_DELAY); });
    connect(ui->minHeightEdit, &QLineEdit::textChanged, this, [this]() { resolutionTimer.start(DEBOUNCE_DELAY); });
    connect(ui->maxWidthEdit, &QLineEdit::textChanged, this, [this]() { resolutionTimer.start(DEBOUNCE_DELAY); });
    connect(ui->minWidthEdit, &QLineEdit::textChanged, this, [this]() { resolutionTimer.start(DEBOUNCE_DELAY); });
    connect(ui->clearResolutionFilterButton, &QPushButton::clicked, this, &MainWindow::clearResolutionFilters);
    connect(&resolutionTimer, &QTimer::timeout, this, &MainWindow::handleResolutionTimerTimeout);

    // sorting controls
    connect(ui->sortComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateSortBy);
    connect(ui->orderComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateSortOrder);
    connect(ui->enableRatioCheckBox, &QCheckBox::toggled, this, &MainWindow::updateEnableRatioSort);
    connect(ui->ratioSlider, &QSlider::valueChanged, this, &MainWindow::updateRatioSlider);
    connect(ui->ratioSlider, &QSlider::valueChanged, this, [this]() { ratioSortTimer.start(SLIDER_DEBOUNCE_DELAY); });
    connect(ui->widthRatioSpinBox, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateRatioSpinBox);
    connect(
        ui->widthRatioSpinBox, &QDoubleSpinBox::valueChanged, this, [this]() { ratioSortTimer.start(SLIDER_DEBOUNCE_DELAY); });
    connect(ui->heightRatioSpinBox, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateRatioSpinBox);
    connect(
        ui->heightRatioSpinBox, &QDoubleSpinBox::valueChanged, this, [this]() { ratioSortTimer.start(SLIDER_DEBOUNCE_DELAY); });
    connect(&ratioSortTimer, &QTimer::timeout, this, &MainWindow::handleRatioTimerTimeout);

    // text search controls
    connect(ui->searchPlatformComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateSearchPlatform);
    connect(ui->searchComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateSearchField);
    connect(ui->searchLineEdit, &QLineEdit::textChanged, this, &MainWindow::updateSearchText);
    connect(ui->searchLineEdit, &QLineEdit::returnPressed, this, &MainWindow::picSearch);
    connect(ui->clearTextSearchButton, &QPushButton::clicked, this, &MainWindow::clearSearchText);

    // tag list controls
    connect(ui->generalTagList, &QListWidget::itemClicked, this, &MainWindow::handleListWidgetItemSingleClick);
    connect(ui->generalTagList, &QListWidget::itemDoubleClicked, this, &MainWindow::addExcludedTags);
    connect(ui->characterTagList, &QListWidget::itemClicked, this, &MainWindow::handleListWidgetItemSingleClick);
    connect(ui->characterTagList, &QListWidget::itemDoubleClicked, this, &MainWindow::addExcludedTags);
    connect(ui->workTagList, &QListWidget::itemClicked, this, &MainWindow::handleListWidgetItemSingleClick);
    connect(ui->workTagList, &QListWidget::itemDoubleClicked, this, &MainWindow::addExcludedTags);
    connect(ui->uncategorizedTagList, &QListWidget::itemClicked, this, &MainWindow::handleListWidgetItemSingleClick);
    connect(ui->uncategorizedTagList, &QListWidget::itemDoubleClicked, this, &MainWindow::addExcludedTags);
    connect(ui->metaTagList, &QListWidget::itemClicked, this, &MainWindow::handleListWidgetItemSingleClick);
    connect(ui->metaTagList, &QListWidget::itemDoubleClicked, this, &MainWindow::addExcludedTags);
    ui->generalTagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->generalTagList, &QListWidget::customContextMenuRequested, this, &MainWindow::handleTagContextMenu);
    ui->characterTagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->characterTagList, &QListWidget::customContextMenuRequested, this, &MainWindow::handleTagContextMenu);
    ui->workTagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->workTagList, &QListWidget::customContextMenuRequested, this, &MainWindow::handleTagContextMenu);
    ui->uncategorizedTagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->uncategorizedTagList, &QListWidget::customContextMenuRequested, this, &MainWindow::handleTagContextMenu);
    ui->metaTagList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->metaTagList, &QListWidget::customContextMenuRequested, this, &MainWindow::handleTagContextMenu);
    connect(&tagClickTimer, &QTimer::timeout, this, &MainWindow::addIncludedTags);
    connect(&tagSearchTimer, &QTimer::timeout, this, &MainWindow::picSearch);

    // tag search box
    connect(ui->searchTagTextEdit, &QLineEdit::textChanged, this, &MainWindow::tagSearch);

    // scroll area scrollbar
    connect(
        ui->picBrowseScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::handleScrollBarValueChanged);

    // menu actions
    connect(ui->importNewPicsAction, &QAction::triggered, this, &MainWindow::handleImportNewPicsAction);
    connect(ui->importPowerfulPixivDownloaderAction,
            &QAction::triggered,
            this,
            &MainWindow::handleImportPowerfulPixivDownloaderAction);
    connect(ui->importGallery_dlTwitterAction, &QAction::triggered, this, &MainWindow::handleImportGallery_dlTwitterAction);
    connect(ui->importExistingDirectoriesAction, &QAction::triggered, this, &MainWindow::handleImportExistingDirectoriesAction);
    connect(ui->showAboutAction, &QAction::triggered, this, &MainWindow::handleShowAboutAction);
    connect(ui->showSettingsAction, &QAction::triggered, this, &MainWindow::handleShowSettingsAction);
    connect(ui->openTagBrowserAction, &QAction::triggered, this, &MainWindow::handleOpenTagBrowserAction);
    connect(ui->startTaggingAction, &QAction::triggered, this, &MainWindow::handleStartTaggingAction);

    // cancel progress
    connect(ui->cancelProgressButton, &QPushButton::clicked, this, &MainWindow::cancelTask);

    // image viewer
}QString getTagString(const TagCount& tagCount) {
    QString tagName = QString::fromUtf8(tagCount.tag.tag.c_str());
    auto& cache = DbCache::getInstance();
    if (cache.hasParents(tagCount.tagId)) tagName += QString::fromUtf8("◂");
    if (cache.hasChildren(tagCount.tagId)) tagName += QString::fromUtf8("▸");
    if (cache.hasAliases(tagCount.tagId)) {
        tagName += QStringLiteral(" [+") + QString::number(cache.getAliases(tagCount.tagId).size()) + QStringLiteral("]");
    }
    if (tagCount.fileCount > 0 && tagCount.count != tagCount.fileCount) {
        return QString("%1 (%2/%3)").arg(tagName).arg(tagCount.count).arg(tagCount.fileCount);
    }
    return QString("%1 (%2)").arg(tagName).arg(tagCount.count);
}
void MainWindow::loadTags() {
    allTags = database.getTagCounts();
    Info() << "Loaded tags from database.";
}
void MainWindow::displayTags(const std::vector<TagCount>& availableTags) {
    ui->characterTagList->clear();
    ui->generalTagList->clear();
    ui->workTagList->clear();
    ui->metaTagList->clear();
    ui->uncategorizedTagList->clear();

    std::vector<TagCount> tagCounts;

    if (availableTags.empty()) {
        tagCounts = allTags;
    } else {
        tagCounts = availableTags;
    }

    auto& cache = DbCache::getInstance();
    std::unordered_map<uint32_t, uint32_t> aliasCountMap;
    std::vector<TagCount> foldedTagCounts;
    for (auto& tc : tagCounts) {
        uint32_t canonicalId = cache.getCanonicalId(tc.tagId);
        if (canonicalId != 0) {
            aliasCountMap[canonicalId] += tc.count;
        } else {
            foldedTagCounts.push_back(tc);
        }
    }
    for (auto& tc : foldedTagCounts) {
        auto it = aliasCountMap.find(tc.tagId);
        if (it != aliasCountMap.end()) {
            tc.count += it->second;
        }
    }
    tagCounts = std::move(foldedTagCounts);

    QStringList characterTagNames, attributeTagNames, workTagNames, metaTagNames, uncategorizedTagNames;
    QList<int> characterTagIndices, attributeTagIndices, workTagIndices, metaTagIndices, uncategorizedTagIndices;

    for (int i = 0; i < (int)tagCounts.size(); i++) {
        auto cat = static_cast<TagCategory>(tagCounts[i].tag.category);
        switch (cat) {
        case TagCategory::Character:
            characterTagNames.append(getTagString(tagCounts[i]));
            characterTagIndices.append(i);
            break;
        case TagCategory::Attribute:
            attributeTagNames.append(getTagString(tagCounts[i]));
            attributeTagIndices.append(i);
            break;
        case TagCategory::Work:
        case TagCategory::Artist:
            workTagNames.append(getTagString(tagCounts[i]));
            workTagIndices.append(i);
            break;
        case TagCategory::Meta:
            metaTagNames.append(getTagString(tagCounts[i]));
            metaTagIndices.append(i);
            break;
        default:
            uncategorizedTagNames.append(getTagString(tagCounts[i]));
            uncategorizedTagIndices.append(i);
            break;
        }
    }

    ui->characterTagList->addItems(characterTagNames);
    for (int i = 0; i < characterTagNames.size(); i++) {
        ui->characterTagList->item(i)->setData(Qt::UserRole, tagCounts[characterTagIndices[i]].tagId);
        ui->characterTagList->item(i)->setData(Qt::UserRole + 1, QString::fromUtf8(tagCounts[characterTagIndices[i]].tag.tag.c_str()));
        ui->characterTagList->item(i)->setData(Qt::UserRole + 2, tagCounts[characterTagIndices[i]].tag.platform);
    }

    ui->generalTagList->addItems(attributeTagNames);
    for (int i = 0; i < attributeTagNames.size(); i++) {
        ui->generalTagList->item(i)->setData(Qt::UserRole, tagCounts[attributeTagIndices[i]].tagId);
        ui->generalTagList->item(i)->setData(Qt::UserRole + 1, QString::fromUtf8(tagCounts[attributeTagIndices[i]].tag.tag.c_str()));
        ui->generalTagList->item(i)->setData(Qt::UserRole + 2, tagCounts[attributeTagIndices[i]].tag.platform);
    }

    ui->workTagList->addItems(workTagNames);
    for (int i = 0; i < workTagNames.size(); i++) {
        ui->workTagList->item(i)->setData(Qt::UserRole, tagCounts[workTagIndices[i]].tagId);
        ui->workTagList->item(i)->setData(Qt::UserRole + 1, QString::fromUtf8(tagCounts[workTagIndices[i]].tag.tag.c_str()));
        ui->workTagList->item(i)->setData(Qt::UserRole + 2, tagCounts[workTagIndices[i]].tag.platform);
    }

    ui->metaTagList->addItems(metaTagNames);
    for (int i = 0; i < metaTagNames.size(); i++) {
        ui->metaTagList->item(i)->setData(Qt::UserRole, tagCounts[metaTagIndices[i]].tagId);
        ui->metaTagList->item(i)->setData(Qt::UserRole + 1, QString::fromUtf8(tagCounts[metaTagIndices[i]].tag.tag.c_str()));
        ui->metaTagList->item(i)->setData(Qt::UserRole + 2, tagCounts[metaTagIndices[i]].tag.platform);
    }

    ui->uncategorizedTagList->addItems(uncategorizedTagNames);
    for (int i = 0; i < uncategorizedTagNames.size(); i++) {
        ui->uncategorizedTagList->item(i)->setData(Qt::UserRole, tagCounts[uncategorizedTagIndices[i]].tagId);
        ui->uncategorizedTagList->item(i)->setData(Qt::UserRole + 1, QString::fromUtf8(tagCounts[uncategorizedTagIndices[i]].tag.tag.c_str()));
        ui->uncategorizedTagList->item(i)->setData(Qt::UserRole + 2, tagCounts[uncategorizedTagIndices[i]].tag.platform);
    }
}
void MainWindow::initTagger() {
    std::vector<std::filesystem::path> taggerPaths = tagger.getExistingTaggerDLLs();
    if (taggerPaths.empty()) {
        Warn() << "No tagger DLLs found. Tagger not initialized.";
        return;
    }
    tagger.loadTaggerDLL(taggerPaths[0]); // TODO: load the first available tagger DLL for now, implement selection later
    Info() << "Tagger initialized with DLL:" << taggerPaths[0];
    Settings::autoTaggerDLLPath = taggerPaths[0];
    if (database.getModelName() != tagger.getModelName()) {
        Info() << "Tagger model name differs from database record. Updating database tag set.";
        tagger.loadTagSetToDatabase();
    }
}

// Functions for filters and sorting

void MainWindow::updateShowUnknowPlatform(bool checked) {
    filterCtx.showUnknowPlatform = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowPixiv(bool checked) {
    filterCtx.showPixiv = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowTwitter(bool checked) {
    filterCtx.showTwitter = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowJPG(bool checked) {
    filterCtx.showJPG = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowPNG(bool checked) {
    filterCtx.showPNG = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowGIF(bool checked) {
    filterCtx.showGIF = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowWEBP(bool checked) {
    filterCtx.showWEBP = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowUnknowRestrict(bool checked) {
    filterCtx.showUnknowRestrict = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowAllAge(bool checked) {
    filterCtx.showAllAge = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowSensitive(bool checked) {
    filterCtx.showSensitive = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowQuestionable(bool checked) {
    filterCtx.showQuestionable = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowR18(bool checked) {
    filterCtx.showR18 = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowR18g(bool checked) {
    filterCtx.showR18G = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowUnknowAI(bool checked) {
    filterCtx.showUnknowAI = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowAI(bool checked) {
    filterCtx.showAI = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateShowNonAI(bool checked) {
    filterCtx.showNonAI = checked;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateMaxWidth(const QString& text) {
    bool ok;
    uint value = text.toUInt(&ok);
    if (ok) {
        filterCtx.maxWidth = value;
    } else {
        filterCtx.maxWidth = std::numeric_limits<uint>::max();
    }
}
void MainWindow::updateMinWidth(const QString& text) {
    bool ok;
    uint value = text.toUInt(&ok);
    if (ok) {
        filterCtx.minWidth = value;
    } else {
        filterCtx.minWidth = 0;
    }
}
void MainWindow::updateMaxHeight(const QString& text) {
    bool ok;
    uint value = text.toUInt(&ok);
    if (ok) {
        filterCtx.maxHeight = value;
    } else {
        filterCtx.maxHeight = std::numeric_limits<uint>::max();
    }
}
void MainWindow::updateMinHeight(const QString& text) {
    bool ok;
    uint value = text.toUInt(&ok);
    if (ok) {
        filterCtx.minHeight = value;
    } else {
        filterCtx.minHeight = 0;
    }
}
void MainWindow::clearResolutionFilters() {
    ui->maxWidthEdit->clear();
    ui->minWidthEdit->clear();
    ui->maxHeightEdit->clear();
    ui->minHeightEdit->clear();
    filterCtx.maxWidth = std::numeric_limits<uint>::max();
    filterCtx.minWidth = 0;
    filterCtx.maxHeight = std::numeric_limits<uint>::max();
    filterCtx.minHeight = 0;
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::handleResolutionTimerTimeout() {
    displayController.setFilterContext(filterCtx);
    picSearch();
}
void MainWindow::updateSortBy(int index) {
    sortCtx.sortBy = static_cast<SortBy>(index);
    displayController.sortDisplayItems(sortCtx);
    displayController.setFilterContext(filterCtx);
}
void MainWindow::updateSortOrder(int index) {
    sortCtx.sortOrder = static_cast<SortOrder>(index);
    displayController.sortDisplayItems(sortCtx);
    displayController.setFilterContext(filterCtx);
}
void MainWindow::updateEnableRatioSort(bool checked) {
    ratioSortEnabled = checked;
    if (ratioSortEnabled) {
        ui->sortComboBox->setDisabled(true);
        ui->orderComboBox->setDisabled(true);
        sortCtx.sortBy = SortBy::Ratio;
    } else {
        ui->sortComboBox->setEnabled(true);
        ui->orderComboBox->setEnabled(true);
        sortCtx.sortBy = static_cast<SortBy>(ui->sortComboBox->currentIndex());
    }
    displayController.sortDisplayItems(sortCtx);
    displayController.setFilterContext(filterCtx);
}
int ratioToSliderValue(double ratio) {
    if (ratio <= 0) {
        return 40;
    } else if (ratio >= 3) {
        return -40;
    }
    if (ratio < 1.0) {
        return static_cast<int>(((1 / (ratio)) - 1) * 20);
    } else {
        return static_cast<int>((1 - ratio) * 20);
    }
}
void MainWindow::updateRatioSlider(int value) {
    if (ratioSpinBoxEditing) return;
    ratioSliderEditing = true;
    ratioSliderValue = value;
    if (value >= 0) {
        double temp = 1.0 + (value / 20.0);
        sortCtx.ratio = 1.0 / temp;
        widthRatioSpinBoxValue = 1.0;
        heightRatioSpinBoxValue = temp;
    } else {
        double temp = 1.0 - (value / 20.0);
        sortCtx.ratio = temp;
        widthRatioSpinBoxValue = temp;
        heightRatioSpinBoxValue = 1.0;
    }
    ui->widthRatioSpinBox->setValue(widthRatioSpinBoxValue);
    ui->heightRatioSpinBox->setValue(heightRatioSpinBoxValue);
    ratioSliderEditing = false;
}
void MainWindow::updateRatioSpinBox(double value) {
    if (ratioSliderEditing) return;
    ratioSpinBoxEditing = true;
    widthRatioSpinBoxValue = ui->widthRatioSpinBox->value();
    heightRatioSpinBoxValue = ui->heightRatioSpinBox->value();
    if (widthRatioSpinBoxValue == 0) {
        sortCtx.ratio = 0;
    } else if (heightRatioSpinBoxValue == 0) {
        sortCtx.ratio = std::numeric_limits<double>::infinity();
    } else {
        sortCtx.ratio = widthRatioSpinBoxValue / heightRatioSpinBoxValue;
    }
    ui->ratioSlider->setValue(ratioToSliderValue(sortCtx.ratio));
    ratioSpinBoxEditing = false;
}
void MainWindow::handleRatioTimerTimeout() {
    if (ratioSortEnabled) {
        displayController.sortDisplayItems(sortCtx);
        displayController.setFilterContext(filterCtx);
    }
}

// Functions for text and tag search

void MainWindow::updateSearchPlatform(int index) {
    searchCtx.searchPlatform = static_cast<PlatformType>(index);
}
void MainWindow::updateSearchField(int index) {
    searchCtx.searchField = static_cast<SearchField>(index);
}
void MainWindow::updateSearchText(const QString& text) {
    searchCtx.searchText = text.toStdString();
}
void MainWindow::clearSearchText() {
    ui->searchLineEdit->clear();
    searchCtx.searchText = "";
    picSearch();
}
void MainWindow::handleListWidgetItemSingleClick(QListWidgetItem* item) {
    if (pendingParentTagId != 0) {
        uint32_t childTagId = item->data(Qt::UserRole).toUInt();
        if (childTagId == pendingParentTagId) {
            statusBar()->showMessage(tr("不能将标签设为自身的父标签"), 3000);
        } else if (database.setTagParent(childTagId, pendingParentTagId)) {
            QString parentName = QString::fromUtf8(database.getStringTag(pendingParentTagId).tag.c_str());
            QString childName = QString::fromUtf8(database.getStringTag(childTagId).tag.c_str());
            statusBar()->showMessage(tr("已建立: %1 → %2").arg(parentName, childName), 3000);
        } else {
            statusBar()->showMessage(tr("建立父子关系失败（可能产生循环）"), 3000);
        }
        pendingParentTagId = 0;
        loadTags();
        displayTags();
        return;
    }
    tagClickTimer.start(DOUBLE_CLICK_DELAY);
    lastClickedTagItem = item;
}
void MainWindow::addIncludedTags() {
    if (tagDoubleClicked) return;

    QListWidgetItem* item = lastClickedTagItem;
    ui->selectedTagScrollArea->show();

    uint32_t tagId = item->data(Qt::UserRole).toUInt();

    QPushButton* tagButton = new QPushButton(this);
    QPalette palette = tagButton->palette();
    palette.setColor(QPalette::Button, QColor(100, 200, 100));
    tagButton->setPalette(palette);
    tagButton->setProperty("tag", tagId);

    if (searchCtx.includedTags.find(tagId) == searchCtx.includedTags.end()) {
        searchCtx.includedTags.insert(tagId);
        tagButton->setText(item->text().left(item->text().lastIndexOf(' ')));
        connect(tagButton, &QPushButton::clicked, this, [this, tagButton]() { removeIncludedTags(tagButton); });
    }
    ui->selectedTagLayout->insertWidget(0, tagButton);
    picSearch();
}
void MainWindow::addExcludedTags(QListWidgetItem* item) {
    tagDoubleClicked = true;
    tagClickTimer.stop();

    ui->selectedTagScrollArea->show();

    uint32_t tagId = item->data(Qt::UserRole).toUInt();

    QPushButton* tagButton = new QPushButton(this);
    QPalette palette = tagButton->palette();
    palette.setColor(QPalette::Button, QColor(200, 100, 100));
    tagButton->setPalette(palette);
    tagButton->setProperty("tag", tagId);

    if (searchCtx.excludedTags.find(tagId) == searchCtx.excludedTags.end()) {
        searchCtx.excludedTags.insert(tagId);
        tagButton->setText(item->text().left(item->text().lastIndexOf(' ')));
        connect(tagButton, &QPushButton::clicked, this, [this, tagButton]() { removeExcludedTags(tagButton); });
    }
    ui->selectedTagLayout->addWidget(tagButton);
    picSearch();
    tagDoubleClicked = false;
}
void MainWindow::removeIncludedTags(QPushButton* button) {
    uint32_t tag = button->property("tag").toUInt();
    searchCtx.includedTags.erase(tag);
    ui->selectedTagLayout->removeWidget(button);
    button->deleteLater();
    if (isSelectedTagsEmpty()) {
        ui->selectedTagScrollArea->hide();
    }
    tagSearchTimer.start(DEBOUNCE_DELAY);
}
void MainWindow::removeExcludedTags(QPushButton* button) {
    uint32_t tag = button->property("tag").toUInt();
    searchCtx.excludedTags.erase(tag);
    ui->selectedTagLayout->removeWidget(button);
    button->deleteLater();
    if (isSelectedTagsEmpty()) {
        ui->selectedTagScrollArea->hide();
    }
    tagSearchTimer.start(DEBOUNCE_DELAY);
}
void MainWindow::tagSearch(const QString& text) {
    QListWidget* currentListWidget = ui->tagListTabWidget->currentWidget()->findChild<QListWidget*>();
    for (int i = 0; i < currentListWidget->count(); i++) {
        QListWidgetItem* item = currentListWidget->item(i);
        if (item->text().contains(text, Qt::CaseInsensitive)) {
            item->setHidden(false);
        } else {
            item->setHidden(true);
        }
    }
}
// Main search function
void MainWindow::picSearch() {
    imageLoader.clearTasks();
    searchCtx.filterCtx = filterCtx;
    searchRequestId++;
    emit searchPics(searchCtx, searchRequestId);
}
void MainWindow::handleSearchResults(DisplayItems* displayItems,
                                     const std::vector<TagCount>& availableTags,
                                     size_t requestId) {
    if (requestId != searchRequestId) return;
    displayController.setDisplayItems(displayItems, searchCtx.searchField);
    displayController.sortDisplayItems(sortCtx);
    displayTags(availableTags);
    ui->statusbar->showMessage("搜索完成，共找到 " +
                               QString::number(displayItems->type == DisplayItemType::Pic ? displayItems->picItems.size()
                                                                                          : displayItems->metadataItems.size()) +
                               " 个结果");
}

// Functions for window resizing and layout

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (firstShow_) {
        firstShow_ = false;
        displayController.handleWindowResize();
        picSearch(); // display no metadata pics initially
    }
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    Settings::setWidthHeight(width(), height());
    displayController.handleWindowResize();
}

// Custom event handling for image loading and import progress

bool MainWindow::event(QEvent* event) {
    if (event->type() == ImageLoadCompleteEvent::EventType) {
        auto* imageEvent = static_cast<ImageLoadCompleteEvent*>(event);
        displayController.displayImage(imageEvent->result.id, imageEvent->result.loadType);
        viewerController.handleImageLoaded(imageEvent->result.id, imageEvent->result.loadType);
        return true;
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        if (handlePictureFrameDoubleClick(nullptr, event)) return true;
    } else if (event->type() == ImportProgressReportEvent::EventType) {
        auto* importProgressEvent = static_cast<ImportProgressReportEvent*>(event);
        displayImportProgress(importProgressEvent->progress, importProgressEvent->total);
        return true;
    } else if (event->type() == TaggingProgressReportEvent::EventType) {
        auto* taggingProgressEvent = static_cast<TaggingProgressReportEvent*>(event);
        displayTaggingProgress(taggingProgressEvent->progress, taggingProgressEvent->total);
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::displayImportProgress(size_t progress, size_t total) {
    if (progress >= total) { // import complete
        finalizeImport(total);
        return;
    }
    // update progress bar and status
    ui->progressBar->setMaximum(static_cast<int>(total));
    ui->progressBar->setValue(static_cast<int>(progress));

    auto currentTime = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - taskStartTime).count();

    if (timeDiff > 0 && progress > 0) {
        double timeDiffSeconds = static_cast<double>(timeDiff) / 1000.0;
        double speed = static_cast<double>(progress) / timeDiffSeconds; // items per second
        size_t etaSeconds = static_cast<size_t>((total - progress) / speed);
        ui->progressStatusLabel->setText(QString("%1 / %2 | 速度：%3 文件每秒 | 剩余时间：%4 秒")
                                             .arg(progress)
                                             .arg(total)
                                             .arg(QString::number(speed, 'f', 2))
                                             .arg(etaSeconds));
    }
}
void MainWindow::finalizeImport(size_t totalImported) {
    if (dirsToImport.empty()) { // single import task
        // record imported directory
        std::filesystem::path importedPath;
        ParserType parserType;
        std::tie(importedPath, parserType) = importer.getImportingDir();
        if (std::find(Settings::picDirectories.begin(), Settings::picDirectories.end(), std::pair{importedPath, parserType}) ==
            Settings::picDirectories.end()) {
            Settings::picDirectories.emplace_back(importedPath, parserType);
        }
        importer.finish();
    } else { // re-importing from multiple directories
        dirsToImport.pop_back();
        importer.finish();
        if (!dirsToImport.empty()) {
            ui->progressBar->setValue(0);
            taskStartTime = std::chrono::steady_clock::now();
            ui->progressStatusLabel->setText(QString("- / - | 速度：- 文件每秒 | 剩余时间：- 秒"));
            importer.startImportFromDirectory(dirsToImport.back().first, dirsToImport.back().second);
            Info() << "Re-importing pictures from directory: " << dirsToImport.back().first.string();
            return;
        }
    }

    // hide progress
    ui->progressWidget->hide();
    ui->progressLabel->setText("");
    ui->progressStatusLabel->setText("");
    ui->statusbar->showMessage(
        "图片导入完成，共扫描 " + QString::number(totalImported) + " 文件，用时 " +
        QString::number(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - taskStartTime).count()) +
        " 秒");

    loadTags(); // load new tags from database
    DbCache::getInstance().clearTagMapping();
    if (isSearchCriteriaEmpty()) {
        picSearch();
    }
    Info() << "Import completed. Total files imported: " << totalImported;

    if (Settings::autoTagAfterImport) {
        Info() << "Starting auto tagging after import...";
        handleStartTaggingAction();
    }
    return;
}
void MainWindow::displayTaggingProgress(size_t progress, size_t total) {
    if (progress >= total) { // tagging complete
        finalizeTagging(total);
        return;
    }
    // update progress bar and status
    ui->progressBar->setMaximum(static_cast<int>(total));
    ui->progressBar->setValue(static_cast<int>(progress));

    auto currentTime = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - taskStartTime).count();

    if (timeDiff > 0 && progress > 0) {
        double timeDiffSeconds = static_cast<double>(timeDiff) / 1000.0;
        double speed = static_cast<double>(progress) / timeDiffSeconds; // items per second
        size_t etaSeconds = static_cast<size_t>((total - progress) / speed);
        ui->progressStatusLabel->setText(QString("%1 / %2 | 速度：%3 文件每秒 | 剩余时间：%4 秒")
                                             .arg(progress)
                                             .arg(total)
                                             .arg(QString::number(speed, 'f', 2))
                                             .arg(etaSeconds));
    }
}
void MainWindow::finalizeTagging(size_t totalTagged) {
    tagger.finish();

    // hide progress
    ui->progressWidget->hide();
    ui->progressLabel->setText("");
    ui->progressStatusLabel->setText("");
    ui->statusbar->showMessage(
        "标签分类完成，共处理 " + QString::number(totalTagged) + " 文件，用时 " +
        QString::number(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - taskStartTime).count()) +
        " 秒");

    loadTags(); // load new tags from database
    Info() << "Tagging completed. Total pics tagged: " << totalTagged;
    return;
}
void MainWindow::cancelTask() {
    if (!haveOngoingTask()) return; // no import task to cancel

    if (!tagger.finish()) {
        ui->statusbar->showMessage("正在取消标签分类任务，请稍候...");
        Info() << "Cancelling tagging task...";

        tagger.forceStop();

        ui->statusbar->showMessage("标签分类任务已取消。");
        Info() << "Tagging task cancelled.";

    } else if (!importer.finish()) {
        ui->statusbar->showMessage("正在取消导入任务，请稍候...");
        Info() << "Cancelling import task...";

        importer.forceStop();
        dirsToImport.clear();

        ui->statusbar->showMessage("导入任务已取消。");
        Info() << "Import task cancelled.";
    }
    ui->progressWidget->hide();
    ui->progressLabel->setText("");
    ui->progressStatusLabel->setText("");
}

// Handlers for menu actions

void MainWindow::handleImportNewPicsAction() {
    if (haveOngoingTask()) {
        ui->statusbar->showMessage("已有任务正在进行中，请稍后再试。");
        return;
    }
    QString dir = QFileDialog::getExistingDirectory(this, "选择图片文件夹", QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;
    ui->statusbar->showMessage("正在扫描文件夹...");
    importer.startImportFromDirectory(std::filesystem::path(dir.toStdString()));
    ui->progressWidget->show();
    ui->progressBar->setValue(0);
    ui->progressLabel->setText("正在导入图片...");
    Info() << "Started importing pictures from directory: " << dir.toStdString();
    taskStartTime = std::chrono::steady_clock::now();
}

void MainWindow::handleImportPowerfulPixivDownloaderAction() {
    if (haveOngoingTask()) {
        ui->statusbar->showMessage("已有任务正在进行中，请稍后再试。");
        return;
    }
    QString dir = QFileDialog::getExistingDirectory(
        this, "选择 Powerful Pixiv Downloader 下载文件夹", QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;

    std::filesystem::path dirPath(dir.toStdString());
    if (!directoryHasMetadata(dirPath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("缺少元数据"),
            tr("该目录没有元数据文件，无法导入标签。\n\n请选择抓取记录文件以生成元数据，或稍后在设置中补充。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            QString fetchRecord = QFileDialog::getOpenFileName(
                this, tr("选择抓取记录文件"), QString(), tr("JSON 文件 (*.json)"));
            if (!fetchRecord.isEmpty()) {
                FetchRecordImportResult result = importer.importTagsFromFetchRecord(dirPath, std::filesystem::path(fetchRecord.toStdString()));
                if (!result.errors.empty()) {
                    QString errorMsg;
                    for (const auto& err : result.errors) {
                        errorMsg += QString::fromUtf8(err.c_str()) + "\n";
                    }
                    QMessageBox::warning(this, tr("导入失败"), tr("生成元数据时出错：\n\n%1\n请重新选择或忽略只导入图片。").arg(errorMsg));
                    reply = QMessageBox::question(
                        this, tr("继续导入"), tr("是否忽略标签，只导入图片？"),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                    if (reply == QMessageBox::No) return;
                } else if (result.matched == 0) {
                    QMessageBox::warning(this, tr("未匹配"), tr("抓取记录中未匹配到任何作品。"));
                    reply = QMessageBox::question(
                        this, tr("继续导入"), tr("是否忽略标签，只导入图片？"),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                    if (reply == QMessageBox::No) return;
                } else {
                    Info() << "Generated" << result.matched << "meta.json files, unmatched:" << result.unmatched;
                }
            }
        }
    }

    ui->statusbar->showMessage("正在扫描 Powerful Pixiv Downloader 下载文件夹...");
    importer.startImportFromDirectory(dirPath, ParserType::PowerfulPixivDownloader);
    ui->progressWidget->show();
    ui->progressBar->setValue(0);
    ui->progressLabel->setText("正在导入Pixiv图片...");
    Info() << "Started importing pixiv pictures from directory: " << dir.toStdString();
    taskStartTime = std::chrono::steady_clock::now();
}
void MainWindow::handleImportGallery_dlTwitterAction() {
    if (haveOngoingTask()) {
        ui->statusbar->showMessage("已有任务正在进行中，请稍后再试。");
        return;
    }
    QString dir =
        QFileDialog::getExistingDirectory(this, "选择 gallery-dl Twitter 下载文件夹", QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;
    ui->statusbar->showMessage("正在扫描 gallery-dl Twitter 下载文件夹...");
    importer.startImportFromDirectory(std::filesystem::path(dir.toStdString()), ParserType::GallerydlTwitter);
    ui->progressWidget->show();
    ui->progressBar->setValue(0);
    ui->progressLabel->setText("正在导入Twitter图片...");
    Info() << "Started importing twitter pictures from directory: " << dir.toStdString();
    taskStartTime = std::chrono::steady_clock::now();
}
void MainWindow::handleImportExistingDirectoriesAction() {
    if (haveOngoingTask()) {
        ui->statusbar->showMessage("已有任务正在进行中，请稍后再试。");
        return;
    }
    ui->statusbar->showMessage("正在重新扫描已导入的图片文件夹...");
    Info() << "Started re-importing pictures from existing directories.";
    for (const auto& dir : Settings::picDirectories) {
        dirsToImport.emplace_back(dir);
    }
    if (dirsToImport.empty()) {
        ui->statusbar->showMessage("没有已导入的图片文件夹可供重新扫描。");
        Info() << "No existing directories to re-import.";
        return;
    }
    importer.startImportFromDirectory(dirsToImport.back().first, dirsToImport.back().second);
    Info() << "Re-importing pictures from directory: " << dirsToImport.back().first.string();
    ui->progressWidget->show();
    ui->progressBar->setValue(0);
    ui->progressLabel->setText("正在重新扫描已导入文件夹...");
    taskStartTime = std::chrono::steady_clock::now();
}
void MainWindow::handleShowAboutAction() {
    if (!aboutDialog) {
        aboutDialog = new AboutDialog(this);
        aboutDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(aboutDialog, &QObject::destroyed, this, [this]() { aboutDialog = nullptr; });
    }
    aboutDialog->show();
    aboutDialog->raise();
    aboutDialog->activateWindow();
}
void MainWindow::handleShowSettingsAction() {
    if (!settingsDialog) {
        settingsDialog = new SettingsDialog(this);
        settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(settingsDialog, &QObject::destroyed, this, [this]() { settingsDialog = nullptr; });
        connect(settingsDialog, &SettingsDialog::importTagsRequested, this, [this](const std::filesystem::path& directory, const std::filesystem::path& fetchRecordPath) {
            FetchRecordImportResult result = importer.importTagsFromFetchRecord(directory, fetchRecordPath);
            if (!result.errors.empty()) {
                QString errorMsg;
                for (const auto& err : result.errors) {
                    errorMsg += QString::fromUtf8(err.c_str()) + "\n";
                }
                QMessageBox::warning(this, tr("导入标签失败"), tr("生成元数据时出错：\n\n%1").arg(errorMsg));
            } else if (result.matched == 0) {
                QMessageBox::warning(this, tr("未匹配"), tr("抓取记录中未匹配到任何作品。"));
            } else {
                QMessageBox::information(this, tr("导入成功"), tr("成功匹配 %1 个作品，未匹配 %2 个。正在重新导入标签...").arg(result.matched).arg(result.unmatched));
                importer.startImportFromDirectory(directory, ParserType::PowerfulPixivDownloader);
                ui->progressWidget->show();
                ui->progressBar->setValue(0);
                ui->progressLabel->setText("正在导入标签...");
                taskStartTime = std::chrono::steady_clock::now();
            }
        });
    }
    settingsDialog->show();
    settingsDialog->raise();
    settingsDialog->activateWindow();
}
void MainWindow::handleOpenTagBrowserAction() {
    if (!tagBrowserDialog) {
        tagBrowserDialog = new TagBrowserDialog(database, this);
        tagBrowserDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(tagBrowserDialog, &QObject::destroyed, this, [this]() { tagBrowserDialog = nullptr; });
        connect(tagBrowserDialog, &QDialog::finished, this, [this]() {
            loadTags();
            displayTags();
        });
    }
    tagBrowserDialog->show();
    tagBrowserDialog->raise();
    tagBrowserDialog->activateWindow();
}
void MainWindow::handleStartTaggingAction() {
    if (haveOngoingTask()) {
        ui->statusbar->showMessage("已有任务正在进行中，请稍后再试。");
        return;
    }
    if (!tagger.taggerLoaded()) {
        Error() << "No tagger loaded. Cannot start tagging.";
        ui->statusbar->showMessage("未找到可用的标签分类器，无法开始标签分类。");
        return;
    }
    ui->statusbar->showMessage("正在对图片进行标签分类...");
    tagger.startTagging();
    ui->progressWidget->show();
    ui->progressBar->setValue(0);
    QString progressLabelText = "正在对图片进行标签分类...";
    if (tagger.gpuAvailable()) {
        progressLabelText += "（GPU 加速）";
    } else {
        progressLabelText += "（CPU）";
    }
    ui->progressLabel->setText(progressLabelText);
    Info() << "Started tagging pictures with tagger.";
    taskStartTime = std::chrono::steady_clock::now();
}

void MainWindow::handleTagContextMenu(const QPoint& pos) {
    QListWidget* listWidget = qobject_cast<QListWidget*>(sender());
    if (!listWidget) return;
    
    QListWidgetItem* item = listWidget->itemAt(pos);
    if (!item) return;
    
    uint32_t tagId = item->data(Qt::UserRole).toUInt();
    QString tagText = item->data(Qt::UserRole + 1).toString();
    int source = item->data(Qt::UserRole + 2).toInt();
    
    QMenu contextMenu(tr("标签管理"), this);
    QAction* characterAction = contextMenu.addAction(tr("设为角色"));
    QAction* attributeAction = contextMenu.addAction(tr("设为属性"));
    QAction* workAction = contextMenu.addAction(tr("设为作品"));
    QAction* metaAction = contextMenu.addAction(tr("设为Meta"));
    QAction* uncategorizedAction = contextMenu.addAction(tr("设为未分类"));
    contextMenu.addSeparator();

    QMenu* parentMenu = contextMenu.addMenu(tr("父标签"));
    QAction* setParentAction = parentMenu->addAction(tr("选择父标签..."));
    parentMenu->addSeparator();
    auto& cache = DbCache::getInstance();
    if (cache.hasParents(tagId)) {
        auto* removeParentSubmenu = parentMenu->addMenu(tr("移除父标签"));
        for (uint32_t pid : cache.getParents(tagId)) {
            TagStr ptag = cache.getStringTag(pid);
            if (ptag.tag.empty()) continue;
            QAction* act = removeParentSubmenu->addAction(QString::fromUtf8(ptag.tag.c_str()));
            act->setData(pid);
        }
    }

    contextMenu.addSeparator();

    QMenu* aliasMenu = contextMenu.addMenu(tr("别名"));
    QAction* addAliasAction = aliasMenu->addAction(tr("添加别名..."));
    if (cache.hasAliases(tagId)) {
        aliasMenu->addSeparator();
        auto* removeAliasSubmenu = aliasMenu->addMenu(tr("移除别名"));
        for (uint32_t aid : cache.getAliases(tagId)) {
            TagStr atag = cache.getStringTag(aid);
            if (atag.tag.empty()) continue;
            QAction* act = removeAliasSubmenu->addAction(QString::fromUtf8(atag.tag.c_str()));
            act->setData(aid);
        }
    }

    contextMenu.addSeparator();
    QAction* deleteAction = contextMenu.addAction(tr("删除标签"));
    
    QAction* selectedAction = contextMenu.exec(listWidget->mapToGlobal(pos));
    
    if (selectedAction == characterAction) {
        database.setTagCategory(tagId, TagCategory::Character);
        if (source != 0) {
            database.classifyPlatformTag(static_cast<PlatformType>(source), tagText.toUtf8().constData(), true);
            database.syncClassifiedPlatformTagsToPictureTags();
        }
        loadTags();
        displayTags();
    } else if (selectedAction == attributeAction) {
        database.setTagCategory(tagId, TagCategory::Attribute);
        if (source != 0) {
            database.classifyPlatformTag(static_cast<PlatformType>(source), tagText.toUtf8().constData(), false);
            database.syncClassifiedPlatformTagsToPictureTags();
        }
        loadTags();
        displayTags();
    } else if (selectedAction == workAction) {
        database.setTagCategory(tagId, TagCategory::Work);
        loadTags();
        displayTags();
    } else if (selectedAction == metaAction) {
        database.setTagCategory(tagId, TagCategory::Meta);
        loadTags();
        displayTags();
    } else if (selectedAction == uncategorizedAction) {
        database.setTagCategory(tagId, TagCategory::Uncategorized);
        loadTags();
        displayTags();
    } else if (selectedAction == setParentAction) {
        TagPickerDialog picker(database, tagId, TagPickerDialog::SelectExisting, this);
        if (picker.exec() == QDialog::Accepted && picker.selectedTagId() != 0) {
            if (database.setTagParent(tagId, picker.selectedTagId())) {
                loadTags();
                displayTags();
            } else {
                QMessageBox::warning(this, tr("操作失败"), tr("无法建立父子关系（可能产生循环）"));
            }
        }
    } else if (selectedAction == addAliasAction) {
        TagPickerDialog picker(database, tagId, TagPickerDialog::SelectExisting, this);
        if (picker.exec() == QDialog::Accepted && picker.selectedTagId() != 0) {
            uint32_t aliasId = picker.selectedTagId();
            QString aliasName = QString::fromUtf8(cache.getStringTag(aliasId).tag.c_str());
            if (database.setTagAlias(aliasId, tagId)) {
                loadTags();
                displayTags();
            } else {
                QMessageBox::warning(this, tr("添加失败"), tr("无法建立别名关系（可能已在同一别名组中）"));
            }
        }
    } else {
        if (selectedAction && selectedAction->parent() && selectedAction->parent()->inherits("QMenu")) {
            auto* grandparentMenu = qobject_cast<QMenu*>(selectedAction->parent()->parent());
            if (grandparentMenu) {
                QString menuTitle = grandparentMenu->title();
                QVariant data = selectedAction->data();
                if (menuTitle == tr("移除父标签") && data.canConvert<uint>()) {
                    database.removeTagParent(tagId, data.toUInt());
                    loadTags();
                    displayTags();
                } else if (menuTitle == tr("移除别名") && data.canConvert<uint>()) {
                    database.removeTagAlias(data.toUInt(), tagId);
                    loadTags();
                    displayTags();
                }
            }
        }
    }

    if (selectedAction == deleteAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("确认删除"),
            tr("确定要删除标签\"%1\"吗？").arg(tagText),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            std::string tagStr = tagText.toUtf8().constData();
            Info() << "Deleting tag:" << tagStr;
            
            database.deleteTag(tagId);
            
            auto platformOpt = database.getPlatformTypeByTagText(tagStr);
            if (platformOpt.has_value()) {
                database.deletePlatformTagClassification(platformOpt.value(), tagStr);
            }
            
            loadTags();
            displayTags();
            Info() << "Tag deleted successfully";
        }
    }
}

void MainWindow::syncClassifiedPlatformTags() {
    database.syncClassifiedPlatformTagsToPictureTags();
    loadTags();
    displayTags();
}

void MainWindow::handleOpenViewer(int displayIndex) {
    const DisplayItems* items = displayController.getDisplayItems();
    if (!items || displayIndex < 0) return;

    viewerController.setup(items, displayIndex, imageLoader, database);
    imageViewer.open(&viewerController);
}

bool MainWindow::handlePictureFrameDoubleClick(QObject* watched, QEvent* event) {
    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    QWidget* widget = QApplication::widgetAt(mouseEvent->globalPosition().toPoint());
    while (widget) {
        auto* frame = qobject_cast<PictureFrame*>(widget);
        if (frame) {
            int displayIndex = displayController.getDisplayIndex(frame);
            if (displayIndex >= 0) {
                handleOpenViewer(displayIndex);
                return true;
            }
        }
        widget = widget->parentWidget();
    }
    return false;
}
