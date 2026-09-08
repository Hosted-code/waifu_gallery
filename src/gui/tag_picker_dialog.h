#pragma once

#include "service/database.h"
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>

class TagPickerDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { SelectExisting, AllowNewText };

    TagPickerDialog(PicDatabase& db, uint32_t excludeTagId, Mode mode, QWidget* parent = nullptr);

    uint32_t selectedTagId() const { return m_selectedTagId; }
    QString inputText() const { return m_inputText; }
    bool isNewText() const { return m_isNewText; }

private:
    PicDatabase& database;
    uint32_t m_excludeTagId;
    Mode m_mode;

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
    QLineEdit* m_newTextEdit = nullptr;
    QPushButton* m_okButton = nullptr;

    uint32_t m_selectedTagId = 0;
    QString m_inputText;
    bool m_isNewText = false;

    void populateList(const QString& filter);
    void onListItemClicked(QListWidgetItem* item);
    void onSearchChanged(const QString& text);
    void onNewTextChanged(const QString& text);
    void accept() override;
};
