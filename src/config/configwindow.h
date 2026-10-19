// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QTabWidget>

class FileNameEditor;
class ShortcutsWidget;
class GeneralConf;
class QFileSystemWatcher;
class VisualsEditor;
class WorkflowsConf;
class QWidget;
class QScrollArea;

class ConfigWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigWindow(QWidget* parent = nullptr);

    // Makes the window no taller than availableHeight; tall tabs scroll.
    void fitToAvailableHeight(int availableHeight);

signals:
    void updateChildren();

protected:
    void keyPressEvent(QKeyEvent*);

private:
    QTabWidget* m_tabWidget;

    FileNameEditor* m_filenameEditor;
    QWidget* m_filenameEditorTab;

    ShortcutsWidget* m_shortcuts;
    QWidget* m_shortcutsTab;

    GeneralConf* m_generalConfig;
    QWidget* m_generalConfigTab;

    VisualsEditor* m_visuals;
    QWidget* m_visualsTab;

    WorkflowsConf* m_workflows;
    QWidget* m_workflowsTab;

    QScrollArea* scrollPage(QWidget* page);
    void initErrorIndicator(QWidget* tab, QWidget* widget);
};
