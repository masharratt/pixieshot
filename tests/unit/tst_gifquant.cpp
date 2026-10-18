// SPDX-License-Identifier: GPL-3.0-or-later
// The animated-background GIF palette: one shared palette, ordered dither
// fixed to the pixel position, clean static shot, exact loop timing.

#include "utils/gifquant.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QTest>
#include <numeric>

namespace {

QImage gradient(QSize size, const QColor& a, const QColor& b)
{
    QImage img(size, QImage::Format_RGB32);
    QLinearGradient g(0, 0, size.width(), size.height());
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    QPainter p(&img);
    p.fillRect(img.rect(), g);
    return img;
}

} // namespace

class TestGifQuant : public QObject
{
    Q_OBJECT

private slots:
    void paletteHasAtMost256Colours()
    {
        const QImage img = gradient(QSize(200, 120), Qt::red, Qt::blue);
        const auto palette =
          gifquant::Palette::fromSamples(gifquant::samplePixels(img, 20000));
        QVERIFY(!palette.colors().isEmpty());
        QVERIFY(palette.colors().size() <= 256);
    }

    void fewColoursAreKeptExactly()
    {
        QVector<QRgb> samples;
        const QRgb colours[] = { qRgb(245, 246, 250), qRgb(236, 72, 96),
                                 qRgb(30, 30, 30) };
        for (int i = 0; i < 300; ++i) {
            samples << colours[i % 3];
        }
        const auto palette = gifquant::Palette::fromSamples(samples);
        QImage flat(40, 20, QImage::Format_RGB32);
        flat.fill(QColor(236, 72, 96));
        const QImage q = gifquant::quantize(flat, palette, flat.rect());
        QCOMPARE(q.format(), QImage::Format_Indexed8);
        for (int y = 0; y < q.height(); ++y) {
            for (int x = 0; x < q.width(); ++x) {
                QCOMPARE(q.pixel(x, y) | 0xff000000u, qRgb(236, 72, 96));
            }
        }
    }

    void gradientStaysClose()
    {
        const QImage img =
          gradient(QSize(300, 160), QColor(0x8B, 0x5C, 0xF6), QColor(0x3B, 0x82, 0xF6));
        const auto palette =
          gifquant::Palette::fromSamples(gifquant::samplePixels(img, 30000));
        const QImage q = gifquant::quantize(img, palette);
        double err = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb a = img.pixel(x, y);
                const QRgb b = q.pixel(x, y);
                err += std::abs(qRed(a) - qRed(b)) +
                       std::abs(qGreen(a) - qGreen(b)) +
                       std::abs(qBlue(a) - qBlue(b));
            }
        }
        err /= 3.0 * img.width() * img.height();
        QVERIFY2(err < 4.0, qPrintable(QString::number(err)));
    }

    // A pixel's index depends only on its colour and position: frames whose
    // backgrounds move keep identical indices wherever nothing changed
    void ditherIsFixedToThePixel()
    {
        QImage a = gradient(QSize(120, 80), QColor(40, 90, 200), QColor(200, 60, 120));
        QImage b = a;
        {
            QPainter p(&b);
            p.fillRect(QRect(0, 0, 40, 80), QColor(10, 200, 40));
        }
        QVector<QRgb> samples = gifquant::samplePixels(a, 10000);
        samples += gifquant::samplePixels(b, 10000);
        const auto palette = gifquant::Palette::fromSamples(samples);
        const QImage qa = gifquant::quantize(a, palette);
        const QImage qb = gifquant::quantize(b, palette);
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 40; x < a.width(); ++x) {
                QCOMPARE(qa.pixelIndex(x, y), qb.pixelIndex(x, y));
            }
        }
    }

    void backgroundIsDitheredShotIsNot()
    {
        // A colour halfway between two palette entries: dithered it mixes
        // both, undithered it is one flat entry
        const QVector<QRgb> samples = { qRgb(100, 100, 100), qRgb(140, 140, 140) };
        const auto palette = gifquant::Palette::fromSamples(samples);
        QImage img(32, 16, QImage::Format_RGB32);
        img.fill(QColor(118, 118, 118));
        const QRect shot(16, 0, 16, 16);
        const QImage q = gifquant::quantize(img, palette, shot);
        QSet<int> left;
        QSet<int> right;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                left.insert(q.pixelIndex(x, y));
                right.insert(q.pixelIndex(x + 16, y));
            }
        }
        QCOMPARE(left.size(), 2);
        QCOMPARE(right.size(), 1);
    }

    void delaysAddUpExactly_data()
    {
        QTest::addColumn<int>("frames");
        QTest::addColumn<double>("seconds");
        QTest::newRow("5 s at 15 fps") << 75 << 5.0;
        QTest::newRow("3 s at 15 fps") << 45 << 3.0;
        QTest::newRow("7 s at 12 fps") << 84 << 7.0;
        QTest::newRow("2 s at 20 fps") << 40 << 2.0;
    }
    void delaysAddUpExactly()
    {
        QFETCH(int, frames);
        QFETCH(double, seconds);
        const QList<int> delays = gifquant::frameDelaysCs(frames, seconds);
        QCOMPARE(delays.size(), frames);
        QCOMPARE(std::accumulate(delays.begin(), delays.end(), 0),
                 qRound(seconds * 100));
        const int lo = int(seconds * 100 / frames);
        for (int d : delays) {
            QVERIFY(d == lo || d == lo + 1);
        }
    }
};

QTEST_MAIN(TestGifQuant)
#include "tst_gifquant.moc"
