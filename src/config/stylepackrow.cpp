// SPDX-License-Identifier: GPL-3.0-or-later

#include "stylepackrow.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Combo item data: path of the pack file
constexpr int kPathRole = Qt::UserRole;

QString fileFilter()
{
    return QObject::tr("Pixieshot style packs (*.%1)")
      .arg(stylepack::fileSuffix());
}

} // namespace

StylePackRow::StylePackRow(QWidget* parent)
  : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* row = new QHBoxLayout();
    m_combo = new QComboBox(this);
    m_combo->setToolTip(
      tr("A style pack holds a whole look: background, padding, corners, "
         "glass, grain, size preset and Animate style."));
    row->addWidget(m_combo, 1);

    auto* apply = new QPushButton(tr("Apply"), this);
    apply->setToolTip(tr("Use the selected pack's look for Beautify and "
                         "Animate."));
    row->addWidget(apply);
    layout->addLayout(row);

    auto* actions = new QHBoxLayout();
    auto* save = new QPushButton(tr("Save current as pack..."), this);
    auto* import = new QPushButton(tr("Import..."), this);
    auto* exportButton = new QPushButton(tr("Export..."), this);
    exportButton->setToolTip(tr("Save the selected pack to a file to share."));
    auto* reveal = new QPushButton(tr("Reveal folder"), this);
    for (auto* b : { save, import, exportButton, reveal }) {
        actions->addWidget(b);
    }
    actions->addStretch();
    layout->addLayout(actions);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    layout->addWidget(m_status);

    connect(apply, &QPushButton::clicked, this, &StylePackRow::applySelected);
    connect(save, &QPushButton::clicked, this, &StylePackRow::saveCurrent);
    connect(import, &QPushButton::clicked, this, &StylePackRow::importPack);
    connect(
      exportButton, &QPushButton::clicked, this, &StylePackRow::exportSelected);
    connect(reveal, &QPushButton::clicked, this, &StylePackRow::revealFolder);

    reload();
}

void StylePackRow::reload(const QString& selectPath)
{
    m_combo->clear();
    for (const stylepack::Entry& e :
         stylepack::listPacks(stylepack::userStylesDir())) {
        const QString label =
          e.builtIn ? tr("%1 (built-in)").arg(e.name) : e.name;
        m_combo->addItem(label, e.path);
    }
    const int index = m_combo->findData(selectPath, kPathRole);
    if (index >= 0) {
        m_combo->setCurrentIndex(index);
    }
}

void StylePackRow::showStatus(const QString& message,
                              const QStringList& warnings)
{
    QString text = message.toHtmlEscaped();
    for (const QString& w : warnings) {
        text += QStringLiteral("<br>&bull; ") + w.toHtmlEscaped();
    }
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void StylePackRow::applySelected()
{
    const QString path = m_combo->currentData(kPathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    const stylepack::ParseResult r = stylepack::loadFile(path);
    if (!r.ok) {
        showStatus(r.error);
        return;
    }
    const QStringList warnings = r.warnings + stylepack::applyToConfig(r.pack);
    emit applied();
    showStatus(tr("Applied \"%1\".").arg(r.pack.name), warnings);
}

bool StylePackRow::writeToStyles(const stylepack::Pack& pack,
                                 QString* writtenPath)
{
    const QString dir = stylepack::userStylesDir();
    if (!QDir().mkpath(dir)) {
        showStatus(tr("Could not create %1.").arg(dir));
        return false;
    }
    const QString path = stylepack::packFilePath(dir, pack.name);
    if (QFileInfo::exists(path) &&
        QMessageBox::question(
          this,
          tr("Replace style pack?"),
          tr("A style pack named \"%1\" already exists. Replace it?")
            .arg(QFileInfo(path).completeBaseName())) != QMessageBox::Yes) {
        return false;
    }
    QString error;
    if (!stylepack::writeFile(path, pack, &error)) {
        showStatus(error);
        return false;
    }
    *writtenPath = path;
    return true;
}

void StylePackRow::saveCurrent()
{
    bool ok = false;
    const QString name =
      QInputDialog::getText(this,
                            tr("Save style pack"),
                            tr("Name for this look:"),
                            QLineEdit::Normal,
                            QString(),
                            &ok)
        .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    QString path;
    if (writeToStyles(stylepack::fromCurrentConfig(name), &path)) {
        reload(path);
        showStatus(tr("Saved \"%1\".").arg(name));
    }
}

void StylePackRow::importPack()
{
    const QString source = QFileDialog::getOpenFileName(
      this,
      tr("Import style pack"),
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
      fileFilter());
    if (source.isEmpty()) {
        return;
    }
    const stylepack::ParseResult r = stylepack::loadFile(source);
    if (!r.ok) {
        showStatus(r.error);
        return;
    }
    // Stores the cleaned pack, so ignored keys never reach the styles folder
    QString path;
    if (writeToStyles(r.pack, &path)) {
        reload(path);
        showStatus(tr("Imported \"%1\". Press Apply to use it.").arg(r.pack.name),
                   r.warnings);
    }
}

void StylePackRow::exportSelected()
{
    const QString path = m_combo->currentData(kPathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    const stylepack::ParseResult r = stylepack::loadFile(path);
    if (!r.ok) {
        showStatus(r.error);
        return;
    }
    const QString start = QDir(QStandardPaths::writableLocation(
                                 QStandardPaths::DesktopLocation))
                            .filePath(stylepack::safeFileName(r.pack.name));
    // The save dialog asks before replacing an existing file
    const QString target = QFileDialog::getSaveFileName(
      this, tr("Export style pack"), start, fileFilter());
    if (target.isEmpty()) {
        return;
    }
    QString error;
    if (stylepack::writeFile(target, r.pack, &error)) {
        showStatus(tr("Exported \"%1\".").arg(r.pack.name), r.warnings);
    } else {
        showStatus(error);
    }
}

void StylePackRow::revealFolder()
{
    const QString dir = stylepack::userStylesDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
