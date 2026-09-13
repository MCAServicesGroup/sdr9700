#include "AboutDialog.h"

#include <QLabel>
#include <QtTest>

class AboutDialogTest final : public QObject
{
    Q_OBJECT

  private slots:
    void presentsProjectIdentityAndAccessibleLink()
    {
        AboutDialog dialog;
        const auto labels = dialog.findChildren<QLabel*>();
        QLabel* description = nullptr;
        QLabel* projectLink = nullptr;
        for (QLabel* label : labels)
        {
            if (label->text() ==
                QStringLiteral(
                    "Native amateur-radio client for the Icom IC-9700 — Linux, Apple Silicon macOS, Qt6, C++"))
            {
                description = label;
            }
            if (label->text().contains(QStringLiteral("github.com/w5jwp/sdr9700")))
            {
                projectLink = label;
            }
        }

        QVERIFY(description != nullptr);
        QVERIFY(description->wordWrap());
        QVERIFY(projectLink != nullptr);
        QVERIFY(projectLink->openExternalLinks());
        QVERIFY(projectLink->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
        QVERIFY(projectLink->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
        QCOMPARE(projectLink->accessibleName(), QStringLiteral("SDR9700 project page"));
        QVERIFY(projectLink->text().contains(QStringLiteral("href=\"https://github.com/w5jwp/sdr9700\"")));
    }
};

QTEST_MAIN(AboutDialogTest)
#include "AboutDialogTest.moc"
