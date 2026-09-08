#pragma once

#include "service/database.h"
#include <QDialog>
#include <QTreeWidget>
#include <unordered_map>

class TagBrowserDialog : public QDialog {
    Q_OBJECT
public:
    explicit TagBrowserDialog(PicDatabase& db, QWidget* parent = nullptr);
    ~TagBrowserDialog() = default;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    PicDatabase& database;
    QTreeWidget* treeWidget = nullptr;
    class QLineEdit* searchEdit = nullptr;
    class QComboBox* categoryFilter = nullptr;

    void buildLayout();
    void populateTree();
    void applyFilter(const QString& text, int category);
    void onItemContextMenu(const QPoint& pos);
    void setTagCategory(uint32_t tagId, TagCategory category);
    void addAliasForTag(uint32_t tagId);
    void deleteTagFromTree(uint32_t tagId);

    void buildSubtree(QTreeWidgetItem* parent, uint32_t parentId,
                      const std::unordered_map<uint32_t, uint32_t>& countMap);
};
