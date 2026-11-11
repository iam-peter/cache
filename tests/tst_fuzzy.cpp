#include "fuzzy.h"

#include <QTest>

class TestFuzzy : public QObject
{
    Q_OBJECT

private slots:
    void fold_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("folded");
        QTest::newRow("case") << "Deploy" << "deploy";
        QTest::newRow("accent") << QStringLiteral("Café") << "cafe";
        QTest::newRow("umlaut") << QStringLiteral("Grüße") << "grusse";
        QTest::newRow("plain") << "staging 2" << "staging 2";
    }
    void fold()
    {
        QFETCH(QString, text);
        QFETCH(QString, folded);
        QCOMPARE(Fuzzy::fold(text), folded);
    }

    void distance_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        QTest::newRow("same") << "deploy" << "deploy" << 0;
        QTest::newRow("swap") << "deplyo" << "deploy" << 1;
        QTest::newRow("missing") << "rollbak" << "rollback" << 1;
        QTest::newRow("extra") << "stagging" << "staging" << 1;
        QTest::newRow("wrong") << "depluy" << "deploy" << 1;
        QTest::newRow("two") << "dpelyo" << "deploy" << 2;
        QTest::newRow("far") << "banana" << "deploy" << 3; // capped at max + 1
    }
    void distance()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(Fuzzy::distance(a, b, 2), expected);
    }

    void match_data()
    {
        QTest::addColumn<QString>("term");
        QTest::addColumn<int>("expected");
        QTest::addColumn<QString>("form");
        const int none = int(Fuzzy::Match::None), typo = int(Fuzzy::Match::Typo), exact = int(Fuzzy::Match::Exact);
        QTest::newRow("exact word") << "deploy" << exact << "deploy";
        QTest::newRow("exact prefix") << "depl" << exact << "deploy";
        QTest::newRow("exact phrase") << "staging review" << exact << "staging review";
        QTest::newRow("accent") << "cafe" << exact << QStringLiteral("Café");
        QTest::newRow("swapped letters") << "deplyo" << typo << "deploy";
        QTest::newRow("missing letter") << "rollbak" << typo << "rollback";
        QTest::newRow("typo while typing") << "rolbac" << typo << "rollback";
        QTest::newRow("two typos, long word") << "stagnig" << typo << "staging";
        QTest::newRow("short word exact only") << "sim" << none << "";
        QTest::newRow("no prefix typo for short") << "note" << none << "";
        QTest::newRow("unrelated") << "banana" << none << "";
        QTest::newRow("phrase with typo") << "stagign review" << none << "";
    }
    void match()
    {
        QFETCH(QString, term);
        QFETCH(int, expected);
        QFETCH(QString, form);
        const Fuzzy::Index index = Fuzzy::index(
            QStringLiteral("We deploy after the staging review at the Café; Sam owns the rollback. Nothing else."));
        QStringList forms;
        QCOMPARE(int(Fuzzy::match(Fuzzy::fold(term), index, &forms)), expected);
        if (!form.isEmpty())
            QVERIFY2(forms.contains(form), qPrintable(forms.join(QLatin1Char(','))));
    }
};

QTEST_APPLESS_MAIN(TestFuzzy)
#include "tst_fuzzy.moc"
