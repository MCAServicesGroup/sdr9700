#include "AboutDialog.h"
#include "AppInfo.h"
#include "DialogFooter.h"
#include "UiTheme.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>

static QPixmap roundedPixmap(const QPixmap& src, int radius)
{
    QPixmap result(src.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(src.rect()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, src);
    return result;
}

AboutDialog::AboutDialog(QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("About %1").arg(QLatin1String(APP_NAME)), parent)
{
    const QString title = QStringLiteral("About %1").arg(QLatin1String(APP_NAME));
    setMinimumSize(360, 320);
    resize(380, 340);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(title, this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(UiTheme::Size::DialogContentMargin, 12, UiTheme::Size::DialogContentMargin, 0);
    contentLayout->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    root->addWidget(content, 1);

    auto* iconLabel = new QLabel(content);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap src(":/images/icons/sdr9700_app_icon.png");
    QPixmap scaled = src.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    iconLabel->setPixmap(roundedPixmap(scaled, 14));
    contentLayout->addWidget(iconLabel);

    auto* appLabel = new QLabel(QString("<b style='font-size:18px'>%1</b>").arg(APP_NAME), content);
    appLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(appLabel);

    auto* verLabel = new QLabel(QString("Version %1").arg(APP_VERSION), content);
    verLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(verLabel);

    contentLayout->addSpacing(8);

    auto* desc = new QLabel(
        QStringLiteral("Native amateur-radio client for the Icom IC-9700 — Linux, Apple Silicon macOS, Qt6, C++"),
        content);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    contentLayout->addWidget(desc);

    auto* projectLink = new QLabel(
        QStringLiteral("<a href=\"https://github.com/w5jwp/sdr9700\">github.com/w5jwp/sdr9700</a>"), content);
    projectLink->setAlignment(Qt::AlignCenter);
    projectLink->setOpenExternalLinks(true);
    projectLink->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    projectLink->setAccessibleName(QStringLiteral("SDR9700 project page"));
    projectLink->setAccessibleDescription(QStringLiteral("Open the SDR9700 project page on GitHub."));
    contentLayout->addWidget(projectLink);

    contentLayout->addStretch(1);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(content);
    footer.buttonBox->addButton(QDialogButtonBox::Close);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    contentLayout->addWidget(footer.widget);
}
