// SPDX-License-Identifier: GPL-3.0-or-later
// Joins overlapping grabs of a scrolling area into one tall image. Pure image
// code: no platform calls, deterministic for the same input frames.

#pragma once

#include <QImage>
#include <QSize>
#include <QVector>

#include <vector>

namespace scrollstitch {

// What one added frame did to the stitched image
enum class Step
{
    Started,    // First frame: it is the whole image so far
    Appended,   // Content moved down and the new rows were added
    NoChange,   // Same content position (or nothing to match on): skipped
    ScrolledUp, // Content moved up: skipped, see ScrollStitcher
    NoMatch,    // No offset fits, or the frame size differs: skipped
    MaxHeight,  // Reached the height limit; the image is final
};

struct Options
{
    // Height limit of the stitched image in pixels
    int maxHeight = 20000;
    // Right-hand columns (in points, scaled by the frame's device pixel
    // ratio) left out of matching, where scroll bars appear and move while
    // scrolling. Clamped to a quarter of the width.
    int ignoreRightPx = 24;
    // Share of the informative overlap rows that must match exactly
    double minMatchRatio = 0.75;
    // Fewest overlapping content rows an offset needs
    int minOverlapRows = 8;
    // Fewest non-blank overlap rows needed to trust an offset
    int minInformativeRows = 4;
};

// Rows kept still at the top (header) and bottom (footer) of every frame
struct Bands
{
    int top = 0;
    int bottom = 0;
    bool operator==(const Bands& o) const
    {
        return top == o.top && bottom == o.bottom;
    }
};

// One hash per row of a 32-bit frame over columns [0, width - ignoreRight).
// uniform[i] is true when every hashed pixel of row i has the same colour.
void hashRows(const QImage& frame,
              int ignoreRight,
              QVector<quint64>& hashes,
              QVector<bool>& uniform);

// Leading and trailing rows whose hashes are equal in a and b (same length).
// When every row is equal, top is the full length and bottom is 0.
Bands sameBands(const QVector<quint64>& a, const QVector<quint64>& b);

struct Match
{
    bool found = false;
    // b row r shows what a row r + offset showed: positive means the
    // content moved up on screen (the user scrolled down)
    int offset = 0;
    // No informative rows to match on
    bool blank = false;
};

// Best vertical offset between frames a and b over the content rows
// [bands.top, n - bands.bottom). Ties go to the smaller |offset|, then to
// the positive one.
Match findOffset(const QVector<quint64>& a,
                 const QVector<quint64>& b,
                 const QVector<bool>& uniformB,
                 const Bands& bands,
                 const Options& options);

// Stitches frames added in order.
//
// Result layout: the first frame's header and content, then each newly
// revealed content row once, then the footer of the last accepted frame.
// Header and footer bands are learnt from the first pair of frames that
// moved, and only ever shrink afterwards; a shrunken footer gives its rows
// back to the content, so a blank area mistaken for a footer heals itself.
//
// Scrolling up is ignored: the reference frame stays the furthest one seen,
// so when the user scrolls back down, appending resumes where it stopped.
// A frame that matches nothing is skipped the same way, so the capture
// recovers once the view overlaps the reference again.
class ScrollStitcher
{
public:
    explicit ScrollStitcher(const Options& options = Options());

    Step add(const QImage& frame);

    // Full stitched image (null before the first frame)
    QImage result() const;
    int height() const;
    int width() const;
    Bands bands() const;
    // Height limit reached: further frames are ignored
    bool isFull() const;
    int framesAccepted() const;
    // Scaled copy that fits inside bound, for a live preview
    QImage preview(const QSize& bound) const;

private:
    int ignoreRightFor(const QImage& frame) const;
    int bodyHeight() const;
    void truncateBody(int rows);
    // Appends rows [from, to) of frame; returns false when the limit cut it
    bool appendRows(const QImage& frame, int from, int to);

    Options m_options;
    QImage m_reference;
    QVector<quint64> m_refHashes;
    QVector<bool> m_refUniform;
    std::vector<QImage> m_strips;
    Bands m_bands;
    bool m_bandsLocked = false;
    bool m_full = false;
    int m_accepted = 0;
};

// Stop rule for automatic scrolling: stop on the height limit, on a frame
// that matches nothing, or after `unchangedLimit` frames in a row that did
// not move down (the end of the content was reached).
class AutoScrollStop
{
public:
    explicit AutoScrollStop(int unchangedLimit = 3);
    bool shouldStop(Step step);
    int unchangedStreak() const;

private:
    int m_limit;
    int m_unchanged = 0;
};

} // namespace scrollstitch
