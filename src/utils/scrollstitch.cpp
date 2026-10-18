// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollstitch.h"

#include <QPainter>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace scrollstitch {

namespace {

constexpr quint64 kFnvOffset = 14695981039346656037ULL;
constexpr quint64 kFnvPrime = 1099511628211ULL;

QImage toRgb32(const QImage& frame)
{
    if (frame.format() == QImage::Format_RGB32 ||
        frame.format() == QImage::Format_ARGB32 ||
        frame.format() == QImage::Format_ARGB32_Premultiplied) {
        return frame;
    }
    return frame.convertToFormat(QImage::Format_RGB32);
}

// Copies rows [from, to) of a 32-bit image into a new RGB32 strip
QImage copyRows(const QImage& src, int from, int to)
{
    QImage strip(src.width(), to - from, QImage::Format_RGB32);
    const int bytes = src.width() * 4;
    for (int y = from; y < to; ++y) {
        std::memcpy(strip.scanLine(y - from), src.constScanLine(y), bytes);
    }
    return strip;
}

} // namespace

void hashRows(const QImage& frame,
              int ignoreRight,
              QVector<quint64>& hashes,
              QVector<bool>& uniform)
{
    const int h = frame.height();
    const int w = frame.width();
    const int skip = std::clamp(ignoreRight, 0, w / 4);
    const int cols = w - skip;
    hashes.resize(h);
    uniform.resize(h);
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const quint32*>(frame.constScanLine(y));
        quint64 hash = kFnvOffset;
        const quint32 first = cols > 0 ? (line[0] & 0x00ffffffu) : 0;
        bool same = true;
        for (int x = 0; x < cols; ++x) {
            // Alpha is ignored: grabs of the same pixels may differ there
            const quint32 px = line[x] & 0x00ffffffu;
            same = same && px == first;
            hash = (hash ^ px) * kFnvPrime;
        }
        hashes[y] = hash;
        uniform[y] = same;
    }
}

Bands sameBands(const QVector<quint64>& a, const QVector<quint64>& b)
{
    const int n = static_cast<int>(std::min(a.size(), b.size()));
    Bands bands;
    while (bands.top < n && a[bands.top] == b[bands.top]) {
        ++bands.top;
    }
    if (bands.top == n) {
        return bands;
    }
    while (bands.bottom < n - bands.top &&
           a[n - 1 - bands.bottom] == b[n - 1 - bands.bottom]) {
        ++bands.bottom;
    }
    return bands;
}

Match findOffset(const QVector<quint64>& a,
                 const QVector<quint64>& b,
                 const QVector<bool>& uniformB,
                 const Bands& bands,
                 const Options& options)
{
    Match best;
    const int n = static_cast<int>(std::min(a.size(), b.size()));
    const int lo = bands.top;
    const int hi = n - bands.bottom;
    const int len = hi - lo;
    const int minOverlap = std::max(1, options.minOverlapRows);
    const int minInformative = std::max(1, options.minInformativeRows);

    int informative = 0;
    for (int r = lo; r < hi; ++r) {
        informative += uniformB[r] ? 0 : 1;
    }
    if (informative < minInformative) {
        best.blank = true;
        return best;
    }

    int bestMatches = -1;
    const int range = len - minOverlap;
    for (int d = -range; d <= range; ++d) {
        // b row r against a row r + d, both inside the content rows
        const int start = std::max(lo, lo - d);
        const int end = std::min(hi, hi - d);
        if (end - start < minOverlap) {
            continue;
        }
        int inf = 0;
        int matches = 0;
        for (int r = start; r < end; ++r) {
            if (uniformB[r]) {
                continue;
            }
            ++inf;
            matches += (b[r] == a[r + d]) ? 1 : 0;
        }
        if (inf < minInformative ||
            matches < options.minMatchRatio * static_cast<double>(inf)) {
            continue;
        }
        const bool better =
          matches > bestMatches ||
          (matches == bestMatches &&
           (std::abs(d) < std::abs(best.offset) ||
            (std::abs(d) == std::abs(best.offset) && d > best.offset)));
        if (better) {
            bestMatches = matches;
            best.found = true;
            best.offset = d;
        }
    }
    return best;
}

ScrollStitcher::ScrollStitcher(const Options& options)
  : m_options(options)
{
    m_options.maxHeight = std::max(1, m_options.maxHeight);
}

// The ignore band is set in points; scroll bars are as wide in points on every
// display, so it is scaled to the frame's device pixels
int ScrollStitcher::ignoreRightFor(const QImage& frame) const
{
    const qreal dpr = frame.devicePixelRatio();
    return qRound(m_options.ignoreRightPx * (dpr > 1.0 ? dpr : 1.0));
}

Step ScrollStitcher::add(const QImage& input)
{
    if (m_full) {
        return Step::MaxHeight;
    }
    if (input.isNull()) {
        return Step::NoMatch;
    }
    const QImage frame = toRgb32(input);

    if (m_reference.isNull()) {
        m_reference = frame;
        hashRows(frame, ignoreRightFor(input), m_refHashes, m_refUniform);
        m_accepted = 1;
        const int rows = std::min(frame.height(), m_options.maxHeight);
        m_strips.push_back(copyRows(frame, 0, rows));
        if (rows >= m_options.maxHeight) {
            m_full = true;
            return Step::MaxHeight;
        }
        return Step::Started;
    }
    if (frame.size() != m_reference.size()) {
        return Step::NoMatch;
    }

    QVector<quint64> hashes;
    QVector<bool> uniform;
    hashRows(frame, ignoreRightFor(input), hashes, uniform);

    const int n = frame.height();
    const Bands pair = sameBands(m_refHashes, hashes);
    if (pair.top == n) {
        return Step::NoChange;
    }
    // Bands only shrink once learnt: a row that changed once is content
    const Bands bands =
      m_bandsLocked ? Bands{ std::min(m_bands.top, pair.top),
                             std::min(m_bands.bottom, pair.bottom) }
                    : pair;

    const Match match = findOffset(m_refHashes, hashes, uniform, bands, m_options);
    if (match.blank) {
        return Step::NoChange;
    }
    if (!match.found) {
        return Step::NoMatch;
    }
    if (match.offset == 0) {
        return Step::NoChange;
    }
    if (match.offset < 0) {
        return Step::ScrolledUp;
    }

    const int oldBottom = m_bandsLocked ? m_bands.bottom : 0;
    m_bands = bands;
    m_bandsLocked = true;
    if (bands.bottom > oldBottom) {
        // First move: the first frame's footer leaves the body
        truncateBody(bands.bottom - oldBottom);
    }
    bool fits = true;
    if (bands.bottom < oldBottom) {
        // Rows that were taken for footer turned out to be content
        fits = appendRows(m_reference, n - oldBottom, n - bands.bottom);
    }
    if (fits) {
        appendRows(frame, n - bands.bottom - match.offset, n - bands.bottom);
    }
    m_reference = frame;
    m_refHashes = hashes;
    m_refUniform = uniform;
    ++m_accepted;
    return m_full ? Step::MaxHeight : Step::Appended;
}

int ScrollStitcher::bodyHeight() const
{
    int h = 0;
    for (const QImage& s : m_strips) {
        h += s.height();
    }
    return h;
}

void ScrollStitcher::truncateBody(int rows)
{
    while (rows > 0 && !m_strips.empty()) {
        QImage& last = m_strips.back();
        if (last.height() <= rows) {
            rows -= last.height();
            m_strips.pop_back();
        } else {
            last = last.copy(0, 0, last.width(), last.height() - rows);
            rows = 0;
        }
    }
}

bool ScrollStitcher::appendRows(const QImage& frame, int from, int to)
{
    const int room = m_options.maxHeight - m_bands.bottom - bodyHeight();
    int count = to - from;
    if (count >= room) {
        count = room;
        m_full = true;
    }
    if (count > 0) {
        m_strips.push_back(copyRows(frame, from, from + count));
    }
    return !m_full;
}

QImage ScrollStitcher::result() const
{
    if (m_strips.empty()) {
        return {};
    }
    const int footer = m_bandsLocked ? m_bands.bottom : 0;
    const int w = m_reference.width();
    QImage out(w, bodyHeight() + footer, QImage::Format_RGB32);
    int y = 0;
    const auto put = [&](const QImage& src, int from, int to) {
        for (int r = from; r < to; ++r) {
            std::memcpy(out.scanLine(y++), src.constScanLine(r), w * 4);
        }
    };
    for (const QImage& s : m_strips) {
        put(s, 0, s.height());
    }
    put(m_reference, m_reference.height() - footer, m_reference.height());
    return out;
}

int ScrollStitcher::height() const
{
    if (m_strips.empty()) {
        return 0;
    }
    return bodyHeight() + (m_bandsLocked ? m_bands.bottom : 0);
}

int ScrollStitcher::width() const
{
    return m_reference.width();
}

Bands ScrollStitcher::bands() const
{
    return m_bandsLocked ? m_bands : Bands();
}

bool ScrollStitcher::isFull() const
{
    return m_full;
}

int ScrollStitcher::framesAccepted() const
{
    return m_accepted;
}

QImage ScrollStitcher::preview(const QSize& bound) const
{
    const int h = height();
    const int w = width();
    if (h <= 0 || w <= 0 || bound.isEmpty()) {
        return {};
    }
    const double scale = std::min(static_cast<double>(bound.width()) / w,
                                  static_cast<double>(bound.height()) / h);
    const QSize size(std::max(1, qRound(w * scale)),
                     std::max(1, qRound(h * scale)));
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    double y = 0;
    for (const QImage& s : m_strips) {
        painter.drawImage(QRectF(0, y * scale, size.width(), s.height() * scale),
                          s);
        y += s.height();
    }
    const int footer = m_bandsLocked ? m_bands.bottom : 0;
    if (footer > 0) {
        painter.drawImage(
          QRectF(0, y * scale, size.width(), footer * scale),
          m_reference,
          QRectF(0, m_reference.height() - footer, w, footer));
    }
    return out;
}

AutoScrollStop::AutoScrollStop(int unchangedLimit)
  : m_limit(std::max(1, unchangedLimit))
{}

bool AutoScrollStop::shouldStop(Step step)
{
    switch (step) {
        case Step::MaxHeight:
        case Step::NoMatch:
            return true;
        case Step::NoChange:
        case Step::ScrolledUp:
            return ++m_unchanged >= m_limit;
        case Step::Started:
        case Step::Appended:
            m_unchanged = 0;
            return false;
    }
    return false;
}

int AutoScrollStop::unchangedStreak() const
{
    return m_unchanged;
}

} // namespace scrollstitch
