// SPDX-License-Identifier: GPL-3.0-or-later
// The snap-and-settle curve must be the real CSS cubic-bezier(.25, 1, .5, 1):
// pinned ends, monotonic, never past 1, and equal to an independent solve.

#include "utils/easing.h"

#include <QTest>
#include <cmath>

namespace {

double bez(double p1, double p2, double s)
{
    const double u = 1.0 - s;
    return 3 * u * u * s * p1 + 3 * u * s * s * p2 + s * s * s;
}

// Reference: dense bisection on x(s) = t with nothing shared with the
// production solver (which uses Newton steps first)
double referenceSolve(double x1, double y1, double x2, double y2, double t)
{
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        (bez(x1, x2, mid) < t ? lo : hi) = mid;
    }
    return bez(y1, y2, 0.5 * (lo + hi));
}

} // namespace

class TestEasing : public QObject
{
    Q_OBJECT

private slots:
    void endsArePinned()
    {
        QCOMPARE(easing::snapSettle(0.0), 0.0);
        QCOMPARE(easing::snapSettle(1.0), 1.0);
        // Out of range clamps
        QCOMPARE(easing::snapSettle(-0.5), 0.0);
        QCOMPARE(easing::snapSettle(1.5), 1.0);
    }

    void monotonicWithoutOvershoot()
    {
        double previous = 0.0;
        for (int i = 1; i <= 1000; ++i) {
            const double y = easing::snapSettle(i / 1000.0);
            QVERIFY2(y >= previous, qPrintable(QString::number(i)));
            QVERIFY2(y <= 1.0, qPrintable(QString::number(y)));
            previous = y;
        }
    }

    void snapsEarly()
    {
        // The curve's character: most of the travel happens early
        QVERIFY(easing::snapSettle(0.25) > 0.6);
        QVERIFY(easing::snapSettle(0.5) > 0.9);
        QVERIFY(easing::snapSettle(0.5) < 1.0);
    }

    void matchesAReferenceSolve_data()
    {
        QTest::addColumn<double>("x1");
        QTest::addColumn<double>("y1");
        QTest::addColumn<double>("x2");
        QTest::addColumn<double>("y2");
        QTest::newRow("snap settle") << 0.25 << 1.0 << 0.5 << 1.0;
        QTest::newRow("css ease") << 0.25 << 0.1 << 0.25 << 1.0;
        QTest::newRow("ease in out") << 0.42 << 0.0 << 0.58 << 1.0;
        QTest::newRow("steep start") << 0.0 << 0.0 << 1.0 << 1.0;
    }
    void matchesAReferenceSolve()
    {
        QFETCH(double, x1);
        QFETCH(double, y1);
        QFETCH(double, x2);
        QFETCH(double, y2);
        for (int i = 0; i <= 200; ++i) {
            const double t = i / 200.0;
            const double got = easing::cubicBezier(x1, y1, x2, y2, t);
            const double want = referenceSolve(x1, y1, x2, y2, t);
            QVERIFY2(std::abs(got - want) < 1e-6,
                     qPrintable(QStringLiteral("t=%1 got %2 want %3")
                                  .arg(t)
                                  .arg(got)
                                  .arg(want)));
        }
    }

    void knownValues()
    {
        // Closed form for this curve: y(s) = 1 - (1 - s)^3, x(s) solved
        // at s = 0.5: x = 3*.25*.25*.5... computed independently below
        const double s = 0.5;
        const double x = bez(0.25, 0.5, s);
        QVERIFY(std::abs(easing::snapSettle(x) - (1 - std::pow(1 - s, 3))) < 1e-9);
    }
};

QTEST_MAIN(TestEasing)
#include "tst_easing.moc"
