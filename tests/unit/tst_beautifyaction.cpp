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

    // Bug: a project saved from a beautified capture opened with "Edit again"
    // saved a plain file; the editor request now carries BEAUTIFY, and the
    // Beautify button must not stack a second pass on top of it.
    void editorWithPendingBeautifyDoesNotStack_bugEditAgainDropsBeautify()
    {
        QCOMPARE(beautifyActionFor(true, false, true),
                 BeautifyAction::AlreadyPendingOnSave);
        QCOMPARE(beautifyActionFor(false, false, true),
                 BeautifyAction::FinishCapture);
    }

    // Bug: with "Beautify every capture" on, every edit+save of an already
    // beautified file nested another frame and shadow.
    void beautifyAlwaysSkipsImagesOpenedInTheEditor_bugDoubleBeautify()
    {
        QVERIFY(shouldBeautify(false, true, false)); // fresh capture
        QVERIFY(!shouldBeautify(false, true, true)); // editing a saved file
        QVERIFY(shouldBeautify(true, false, true));  // explicit request wins
        QVERIFY(!shouldBeautify(false, false, false));
    }
};

QTEST_MAIN(TestBeautifyAction)
#include "tst_beautifyaction.moc"
