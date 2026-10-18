// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the rule that decides what the Beautify button does.

#include "tools/beautify/beautifyaction.h"

#include <QTest>

class TestBeautifyAction : public QObject
{
    Q_OBJECT
private slots:
    // Bug: pressing Beautify in the editor window closed the window and left
    // only the saved-capture thumbnail; the user had to click it to reopen
    // the editor. In the editor it must apply in place and keep the window.
    void editorBeautifyKeepsWindowOpen_bugEditorClosesToToast()
    {
        QCOMPARE(beautifyActionFor(true, false), BeautifyAction::ApplyInEditor);
    }

    // A second press right after Beautify reverts it (toggle), it never
    // adds a second round of padding
    void editorSecondPressReverts()
    {
        QCOMPARE(beautifyActionFor(true, true), BeautifyAction::RevertInEditor);
    }

    // The capture overlay keeps finishing like Accept
    void overlayFinishesCapture()
    {
        QCOMPARE(beautifyActionFor(false, false), BeautifyAction::FinishCapture);
        QCOMPARE(beautifyActionFor(false, true), BeautifyAction::FinishCapture);
    }
};

QTEST_MAIN(TestBeautifyAction)
#include "tst_beautifyaction.moc"
