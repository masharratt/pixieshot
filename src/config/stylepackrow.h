// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/stylepack.h"

#include <QWidget>

class QComboBox;
class QLabel;

// "Style pack" row of the Effects group: pick a built-in or user pack and
// apply it, save the current look as a pack, import, export, reveal folder.
// Emits applied() after the config changed so the owner can refresh every
// Effects / Animate control and preview.
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

    QComboBox* m_combo = nullptr;
    QLabel* m_status = nullptr;
};
