#include "AboutDialog.h"

#include <QLabel>
#include <QtTest>
#include <algorithm>

class AboutDialogTest final : public QObject
{
    Q_OBJECT

  private slots:
    void presentsProjectIdentityAndAccessibleLink()
    {
        AboutDialog dialog;
        QCOMPARE(dialog.windowTitle(), QStringLiteral("About SDR9700"));
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
            if (label->text().contains(QStringLiteral("github.com/w5jwp/SDR9700")))
            {
                projectLink = label;
            }
        }

        QVERIFY(description != nullptr);
        QVERIFY(std::any_of(labels.cbegin(), labels.cend(),
                            [](const QLabel* label) { return label->text().contains(QStringLiteral("SDR9700")); }));
        QVERIFY(description->wordWrap());
        QVERIFY(projectLink != nullptr);
        QVERIFY(projectLink->openExternalLinks());
        QVERIFY(projectLink->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
        QVERIFY(projectLink->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
        QCOMPARE(projectLink->accessibleName(), QStringLiteral("SDR9700 project page"));
        QVERIFY(projectLink->text().contains(QStringLiteral("href=\"https://github.com/w5jwp/SDR9700\"")));
    }
};

QTEST_MAIN(AboutDialogTest)
#include "AboutDialogTest.moc"
