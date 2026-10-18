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

    // Bug: in the editor, Animate pressed right after Beautify-in-place
    // animated the already beautified image, giving a frame inside a frame.
    // It must start from the image as it was before Beautify.
    void animateAfterInPlaceBeautifyUsesThePreBeautifyImage_bugFrameInFrame()
    {
        QCOMPARE(animateBaseFor(true, true, false), AnimateBase::BeforeBeautify);
        // A canvas change after Beautify does not matter when it is newest
        QCOMPARE(animateBaseFor(true, true, true), AnimateBase::BeforeBeautify);
    }

    // Same bug with annotations drawn after Beautify: the shot area of the
    // current image (un-beautified pixels plus the new annotations) is used
    void animateWithStepsAfterBeautifyUsesTheShotArea_bugFrameInFrame()
    {
        QCOMPARE(animateBaseFor(true, false, false), AnimateBase::BeautifiedShot);
        // The canvas grew since, so the shot area moved: fall back to the
        // image before Beautify
        QCOMPARE(animateBaseFor(true, false, true), AnimateBase::BeforeBeautify);
    }

    // No in-place Beautify: the editor shows the plain base. That includes a
    // project re-opened as beautified (BEAUTIFY pending on save), whose base
    // is stored un-beautified, so Animate applies the effects exactly once.
    void animateWithoutInPlaceBeautifyUsesTheCurrentImage()
    {
        QCOMPARE(animateBaseFor(false, false, false), AnimateBase::CurrentImage);
        QCOMPARE(animateBaseFor(false, false, true), AnimateBase::CurrentImage);
    }
};

QTEST_MAIN(TestBeautifyAction)
#include "tst_beautifyaction.moc"
