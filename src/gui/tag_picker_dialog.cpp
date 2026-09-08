#include "tag_picker_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TagPickerDialog::TagPickerDialog(PicDatabase& db, uint32_t excludeTagId, Mode mode, QWidget* parent)
    : QDialog(parent), database(db), m_excludeTagId(excludeTagId), m_mode(mode) {
    setWindowTitle(mode == AllowNewText ? tr("选择标签或输入新文本") : tr("选择标签"));
    setMinimumSize(400, 500);

    auto* layout = new QVBoxLayout(this);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("搜索..."));
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget, 1);

    if (mode == AllowNewText) {
        auto* newLayout = new QHBoxLayout();
        newLayout->addWidget(new QLabel(tr("或输入新文本:"), this));
        m_newTextEdit = new QLineEdit(this);
        newLayout->addWidget(m_newTextEdit, 1);
        layout->addLayout(newLayout);
    }

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagPickerDialog::onSearchChanged);
    connect(m_listWidget, &QListWidget::itemClicked, this, &TagPickerDialog::onListItemClicked);
    if (m_newTextEdit) {
        connect(m_newTextEdit, &QLineEdit::textChanged, this, &TagPickerDialog::onNewTextChanged);
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TagPickerDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateList("");
}

void TagPickerDialog::populateList(const QString& filter) {
    m_listWidget->clear();
    auto& cache = DbCache::getInstance();
    const auto& allTags = database.getAllTags();

    for (const auto& tag : allTags) {
        uint32_t tagId = cache.getTagId(tag.tag);
        if (tagId == 0 || tagId == m_excludeTagId) continue;

        QString displayName = QString::fromUtf8(tag.tag.c_str());
        if (!filter.isEmpty() && !displayName.contains(filter, Qt::CaseInsensitive)) continue;

        auto* item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, tagId);
        m_listWidget->addItem(item);
    }
}

void TagPickerDialog::onSearchChanged(const QString& text) {
    populateList(text);
}

void TagPickerDialog::onListItemClicked(QListWidgetItem* item) {
    m_selectedTagId = item->data(Qt::UserRole).toUInt();
    m_isNewText = false;
    if (m_newTextEdit) m_newTextEdit->clear();
    m_okButton->setEnabled(true);
}

void TagPickerDialog::onNewTextChanged(const QString& text) {
    if (!text.trimmed().isEmpty()) {
        m_listWidget->clearSelection();
        m_selectedTagId = 0;
        m_isNewText = true;
        m_okButton->setEnabled(true);
    } else if (m_selectedTagId == 0) {
        m_okButton->setEnabled(false);
    }
}

void TagPickerDialog::accept() {
    if (m_isNewText && m_newTextEdit) {
        m_inputText = m_newTextEdit->text().trimmed();
    }
    QDialog::accept();
}
