// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/stylepack.h"

#include <QHash>
#include <QPixmap>
#include <QWidget>

class QComboBox;
class QLabel;

// "Style pack" row of the Effects group: pick a built-in or user pack and
// apply it, save the current look as a pack, import, export, reveal folder.
// Emits applied() after the config changed so the owner can refresh every
// Effects / Animate control and preview. Under the combo a live preview of the
// selected pack (a Beautify still and an Animate frame of a sample shot), its
// one-line description and its key ingredients; nothing is written to the
// config until Apply.
class StylePackRow : public QWidget
{
    Q_OBJECT
public:
    explicit StylePackRow(QWidget* parent = nullptr);

signals:
    void applied();

private:
    void reload(const QString& selectPath = {});
    void applySelected();
    void saveCurrent();
    void importPack();
    void exportSelected();
    void revealFolder();
    // Non-blocking status under the row; warnings listed one per line
    void showStatus(const QString& message, const QStringList& warnings = {});
    // Writes into the styles folder, asking before replacing a file
    bool writeToStyles(const stylepack::Pack& pack, QString* writtenPath);

    // Shows the selected pack's preview, description and ingredients
    void updatePreview();

    struct Thumbs
    {
        QPixmap beautify;
        QPixmap animate;
        QString description;
        QString chips;
    };
    // Rendered small once per pack file; dropped when a pack is applied,
    // since a pack that leaves a key out falls back to the saved value
    QHash<QString, Thumbs> m_cache;

    QComboBox* m_combo = nullptr;
    QLabel* m_beautify = nullptr;
    QLabel* m_animate = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_chips = nullptr;
    QLabel* m_status = nullptr;
};
