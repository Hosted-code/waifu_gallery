#include "tag_browser_dialog.h"
#include "tag_picker_dialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TagBrowserDialog::TagBrowserDialog(PicDatabase& db, QWidget* parent)
    : QDialog(parent), database(db) {
    setWindowTitle(tr("标签浏览器"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(800, 600);
    buildLayout();
    populateTree();
}

void TagBrowserDialog::closeEvent(QCloseEvent* event) {
    QDialog::closeEvent(event);
}

void TagBrowserDialog::buildLayout() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    auto* searchLabel = new QLabel(tr("搜索:"), this);
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(tr("按标签名或别名过滤..."));
    categoryFilter = new QComboBox(this);
    categoryFilter->addItem(tr("全部分类"), -1);
    categoryFilter->addItem(tr("角色"), static_cast<int>(TagCategory::Character));
    categoryFilter->addItem(tr("属性"), static_cast<int>(TagCategory::Attribute));
    categoryFilter->addItem(tr("作品"), static_cast<int>(TagCategory::Work));
    categoryFilter->addItem(tr("Meta"), static_cast<int>(TagCategory::Meta));
    categoryFilter->addItem(tr("未分类"), static_cast<int>(TagCategory::Uncategorized));

    topBar->addWidget(searchLabel);
    topBar->addWidget(searchEdit, 1);
    topBar->addWidget(new QLabel(tr("分类:"), this));
    topBar->addWidget(categoryFilter);
    mainLayout->addLayout(topBar);

    treeWidget = new QTreeWidget(this);
    treeWidget->setColumnCount(4);
    treeWidget->setHeaderLabels({tr("标签"), tr("分类"), tr("数量"), tr("别名")});
    treeWidget->header()->setStretchLastSection(true);
    treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
    treeWidget->setDragEnabled(true);
    treeWidget->setAcceptDrops(true);
    treeWidget->setDropIndicatorShown(true);
    treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    treeWidget->setAlternatingRowColors(true);

    mainLayout->addWidget(treeWidget, 1);

    auto* buttonRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(tr("刷新"), this);
    buttonRow->addStretch();
    buttonRow->addWidget(refreshBtn);
    mainLayout->addLayout(buttonRow);

    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
        applyFilter(t, categoryFilter->currentData().toInt());
    });
    connect(categoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applyFilter(searchEdit->text(), categoryFilter->currentData().toInt());
    });
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        database.refreshTagMapping();
        populateTree();
    });
    connect(treeWidget, &QTreeWidget::customContextMenuRequested, this, &TagBrowserDialog::onItemContextMenu);
}

static QString categoryLabel(int cat) {
    switch (static_cast<TagCategory>(cat)) {
    case TagCategory::Character: return QStringLiteral("角色");
    case TagCategory::Attribute: return QStringLiteral("属性");
    case TagCategory::Work: return QStringLiteral("作品");
    case TagCategory::Artist: return QStringLiteral("画师");
    case TagCategory::Meta: return QStringLiteral("Meta");
    default: return QStringLiteral("未分类");
    }
}

void TagBrowserDialog::buildSubtree(QTreeWidgetItem* parent, uint32_t parentId,
                                    const std::unordered_map<uint32_t, uint32_t>& countMap) {
    auto& cache = DbCache::getInstance();
    const auto& children = cache.getChildren(parentId);
    for (uint32_t childId : children) {
        TagStr ts = cache.getStringTag(childId);
        if (ts.tag.empty()) continue;

        QStringList cols;
        QString displayName = QString::fromUtf8(ts.tag.c_str());
        if (cache.hasChildren(childId)) displayName += QStringLiteral(" ▸");
        cols << displayName;
        cols << categoryLabel(ts.category);
        auto cit = countMap.find(childId);
        cols << QString::number(cit != countMap.end() ? cit->second : 0);

        const auto& aliasIds = cache.getAliases(childId);
        QStringList aliasList;
        for (uint32_t aid : aliasIds) {
            TagStr atag = cache.getStringTag(aid);
            if (!atag.tag.empty()) aliasList << QString::fromUtf8(atag.tag.c_str());
        }
        cols << aliasList.join(", ");

        auto* item = new QTreeWidgetItem(parent, cols);
        item->setData(0, Qt::UserRole, childId);
        item->setData(0, Qt::UserRole + 1, ts.category);
        buildSubtree(item, childId, countMap);
        item->setExpanded(true);
    }
}

void TagBrowserDialog::populateTree() {
    treeWidget->clear();

    auto& cache = DbCache::getInstance();
    const auto& allTags = database.getAllTags();
    const auto& tagCounts = database.getTagCounts();

    std::unordered_map<uint32_t, uint32_t> countMap;
    for (const auto& tc : tagCounts) countMap[tc.tagId] = tc.count;

    for (const auto& tag : allTags) {
        uint32_t tagId = cache.getTagId(tag.tag);
        if (tagId == 0) continue;
        if (cache.hasParents(tagId)) continue;

        QStringList cols;
        QString displayName = QString::fromUtf8(tag.tag.c_str());
        if (cache.hasChildren(tagId)) displayName += QStringLiteral(" ▸");
        cols << displayName;
        cols << categoryLabel(tag.category);
        auto cit = countMap.find(tagId);
        cols << QString::number(cit != countMap.end() ? cit->second : 0);

        const auto& aliasIds = cache.getAliases(tagId);
        QStringList aliasList;
        for (uint32_t aid : aliasIds) {
            TagStr atag = cache.getStringTag(aid);
            if (!atag.tag.empty()) aliasList << QString::fromUtf8(atag.tag.c_str());
        }
        cols << aliasList.join(", ");

        auto* item = new QTreeWidgetItem(treeWidget, cols);
        item->setData(0, Qt::UserRole, tagId);
        item->setData(0, Qt::UserRole + 1, tag.category);
        buildSubtree(item, tagId, countMap);
        item->setExpanded(true);
    }
}

void TagBrowserDialog::applyFilter(const QString& text, int category) {
    QTreeWidgetItemIterator it(treeWidget);
    while (*it) {
        QTreeWidgetItem* item = *it;
        bool matches = true;
        if (!text.isEmpty()) {
            bool textMatch = item->text(0).contains(text, Qt::CaseInsensitive) ||
                              item->text(3).contains(text, Qt::CaseInsensitive);
            matches = textMatch;
        }
        if (matches && category != -1) {
            int itemCat = item->data(0, Qt::UserRole + 1).toInt();
            if (itemCat != category) matches = false;
        }
        item->setHidden(!matches);
        QTreeWidgetItem* p = item->parent();
        while (p) { p->setHidden(false); p = p->parent(); }
        ++it;
    }
}

void TagBrowserDialog::onItemContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = treeWidget->itemAt(pos);
    if (!item) return;

    uint32_t tagId = item->data(0, Qt::UserRole).toUInt();
    int cat = item->data(0, Qt::UserRole + 1).toInt();
    QString tagName = item->text(0);
    tagName.remove(QStringLiteral(" ▸"));

    QMenu menu(tr("标签操作"), this);
    QAction* charAction = menu.addAction(tr("设为角色"));
    QAction* attrAction = menu.addAction(tr("设为属性"));
    QAction* workAction = menu.addAction(tr("设为作品"));
    QAction* metaAction = menu.addAction(tr("设为Meta"));
    QAction* uncatAction = menu.addAction(tr("设为未分类"));
    menu.addSeparator();

    auto& cache = DbCache::getInstance();

    QMenu* parentMenu = menu.addMenu(tr("父标签"));
    QAction* setParentAction = parentMenu->addAction(tr("选择父标签..."));
    parentMenu->addSeparator();
    if (cache.hasParents(tagId)) {
        auto* removeParentSub = parentMenu->addMenu(tr("移除父标签"));
        for (uint32_t pid : cache.getParents(tagId)) {
            TagStr ptag = cache.getStringTag(pid);
            if (ptag.tag.empty()) continue;
            QAction* act = removeParentSub->addAction(QString::fromUtf8(ptag.tag.c_str()));
            act->setData(pid);
        }
    }

    menu.addSeparator();

    QMenu* aliasMenu = menu.addMenu(tr("别名"));
    QAction* addAliasAction = aliasMenu->addAction(tr("添加别名..."));
    if (cache.hasAliases(tagId)) {
        aliasMenu->addSeparator();
        auto* removeAliasSub = aliasMenu->addMenu(tr("移除别名"));
        for (uint32_t aid : cache.getAliases(tagId)) {
            TagStr atag = cache.getStringTag(aid);
            if (atag.tag.empty()) continue;
            QAction* act = removeAliasSub->addAction(QString::fromUtf8(atag.tag.c_str()));
            act->setData(aid);
        }
    }

    menu.addSeparator();
    QAction* deleteAction = menu.addAction(tr("删除标签"));

    QAction* selected = menu.exec(treeWidget->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == charAction) setTagCategory(tagId, TagCategory::Character);
    else if (selected == attrAction) setTagCategory(tagId, TagCategory::Attribute);
    else if (selected == workAction) setTagCategory(tagId, TagCategory::Work);
    else if (selected == metaAction) setTagCategory(tagId, TagCategory::Meta);
    else if (selected == uncatAction) setTagCategory(tagId, TagCategory::Uncategorized);
    else if (selected == setParentAction) {
        TagPickerDialog picker(database, tagId, TagPickerDialog::SelectExisting, this);
        if (picker.exec() == QDialog::Accepted && picker.selectedTagId() != 0) {
            if (database.setTagParent(tagId, picker.selectedTagId())) {
                populateTree();
            } else {
                QMessageBox::warning(this, tr("操作失败"), tr("无法建立父子关系（可能产生循环）"));
            }
        }
    }
    else if (selected == addAliasAction) addAliasForTag(tagId);
    else if (selected == deleteAction) deleteTagFromTree(tagId);
    else {
        if (selected->parent() && selected->parent()->inherits("QMenu")) {
            auto* gp = qobject_cast<QMenu*>(selected->parent()->parent());
            if (gp) {
                QString title = gp->title();
                QVariant data = selected->data();
                if (title == tr("移除父标签") && data.canConvert<uint>()) {
                    database.removeTagParent(tagId, data.toUInt());
                    populateTree();
                } else if (title == tr("移除别名") && data.canConvert<uint>()) {
                    database.removeTagAlias(data.toUInt(), tagId);
                    populateTree();
                }
            }
        }
    }
}

void TagBrowserDialog::setTagCategory(uint32_t tagId, TagCategory category) {
    database.setTagCategory(tagId, category);
    populateTree();
}

void TagBrowserDialog::addAliasForTag(uint32_t tagId) {
    TagPickerDialog picker(database, tagId, TagPickerDialog::SelectExisting, this);
    if (picker.exec() != QDialog::Accepted || picker.selectedTagId() == 0) return;

    uint32_t aliasId = picker.selectedTagId();
    if (!database.setTagAlias(aliasId, tagId)) {
        QMessageBox::warning(this, tr("添加失败"), tr("无法建立别名关系（可能已在同一别名组中）"));
        return;
    }
    populateTree();
}

void TagBrowserDialog::deleteTagFromTree(uint32_t tagId) {
    auto& cache = DbCache::getInstance();
    TagStr ts = cache.getStringTag(tagId);
    auto reply = QMessageBox::question(this, tr("确认删除"),
        tr("确定要删除标签\"%1\"吗？").arg(QString::fromUtf8(ts.tag.c_str())),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    database.deleteTag(tagId);
    populateTree();
}
