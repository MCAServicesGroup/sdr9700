#pragma once

#include <algorithm>
#include <QColor>
#include <QString>

namespace UiTheme
{
namespace Color
{
inline constexpr const char* Accent = "#00b4d8";
inline constexpr const char* AccentDark = "#0d4f68";
inline constexpr const char* AccentHover = "#11617d";
inline constexpr const char* AccentBright = "#38d8ff";
inline constexpr const char* ControlActive = "#2b4355";
inline constexpr const char* ControlActiveHover = "#3c596f";
inline constexpr const char* ControlActiveBorder = "#8eabc1";
inline constexpr const char* CompoundControlBorder = "#71808e";
inline constexpr const char* ControlPressed = "#252c33";
inline constexpr const char* ControlActiveGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #52728c, stop:0.48 #3e5d75, stop:1 #2e485d)";
inline constexpr const char* ControlNeutralGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #414a53, stop:0.48 #333b43, stop:1 #262c32)";
inline constexpr const char* ControlNeutralHoverGradient =
    "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4c5863, stop:0.48 #3a454e, stop:1 #2c343b)";
inline constexpr const char* ScrollHandle = "#3e5d75";
inline constexpr const char* ScrollHandleHover = "#52728c";
inline constexpr const char* ScrollHandlePressed = "#63839c";
inline constexpr const char* ScrollBorderHover = "#a9bfd0";
inline constexpr const char* ScrollBorderPressed = "#d1dce5";

inline constexpr const char* Panel = "#1d1f24";
inline constexpr const char* PanelDark = "#1a1e24";
inline constexpr const char* WindowChrome = "#1e1e1e";
inline constexpr const char* WindowChromeShadow = "#14181d";
inline constexpr const char* ContentBackground = "#1d2329";
inline constexpr const char* MenuBar = "#242b33";
inline constexpr const char* MenuPanel = "#20242b";
inline constexpr const char* Field = "#10151c";
inline constexpr const char* MeterTrough = "#0f141a";

inline constexpr const char* Border = "#333943";
inline constexpr const char* BorderMedium = "#404854";
inline constexpr const char* BorderLight = "#4a4e58";
inline constexpr const char* BorderFocus = "#586476";
inline constexpr const char* Separator = "#304050";
inline constexpr const char* StatusBorder = "#2a303a";
inline constexpr const char* ControlStripBorder = "#303640";

inline constexpr const char* TextPrimary = "#e0e6ec";
inline constexpr const char* TextBright = "#e8f8ff";
inline constexpr const char* TextField = "#f0f5f8";
inline constexpr const char* TextMuted = "#aebdcc";
inline constexpr const char* TextStatusPrimary = "#c8d8e8";
inline constexpr const char* TextStatusSecondary = "#8ea8c0";
inline constexpr const char* TextStatusLabel = "#506070";

inline constexpr const char* Button = "#30333a";
inline constexpr const char* ButtonHover = "#3b3f48";
inline constexpr const char* ButtonHoverBorder = "#6a7080";

inline constexpr const char* PttButton = "#343039";
inline constexpr const char* PttBorder = "#6a4e58";
inline constexpr const char* PttHover = "#463944";
inline constexpr const char* PttHoverBorder = "#8a6870";
inline constexpr const char* PttActive = "#cc2200";
inline constexpr const char* PttActiveBorder = "#ff6644";

inline constexpr const char* Success = "#4dd87a";
inline constexpr const char* Warning = "#ffb84d";
inline constexpr const char* Danger = "#ff4d4d";
inline constexpr const char* White = "#ffffff";

inline constexpr QColor MeterFrame{0x3f, 0x47, 0x52};
inline constexpr QColor MeterTroughQColor{0x0f, 0x14, 0x1a};
inline constexpr QColor MeterBlue{0x1f, 0x76, 0xff};
inline constexpr QColor MeterCyan{0x00, 0xb4, 0xd8};
inline constexpr QColor MeterGreen{0x4d, 0xd8, 0x7a};
inline constexpr QColor MeterAmber{0xff, 0xb8, 0x4d};
inline constexpr QColor MeterRed{0xff, 0x4d, 0x4d};
inline constexpr QColor MeterScaleText{0x7f, 0xa4, 0xc8};
inline constexpr QColor ScopeShelfEdge{0x2a, 0x40, 0x4f};
inline constexpr QColor BorderLightQColor{0x4a, 0x4e, 0x58};
inline constexpr QColor AccentBrightQColor{0x38, 0xd8, 0xff};
inline constexpr QColor TextPrimaryQColor{0xe0, 0xe6, 0xec};
inline constexpr QColor TextStatusSecondaryQColor{0x8e, 0xa8, 0xc0};
inline constexpr QColor DangerQColor{0xff, 0x4d, 0x4d};
} // namespace Color

inline QColor spectrumSignalColor(double strength)
{
    const double normalized = std::clamp(strength, 0.0, 1.0);
    if (normalized < 0.25)
    {
        return QColor::fromRgbF(0.0, normalized / 0.25, 1.0);
    }
    if (normalized < 0.50)
    {
        const double position = (normalized - 0.25) / 0.25;
        return QColor::fromRgbF(0.0, 1.0, 1.0 - position);
    }
    if (normalized < 0.75)
    {
        const double position = (normalized - 0.50) / 0.25;
        return QColor::fromRgbF(position, 1.0, 0.0);
    }

    const double position = (normalized - 0.75) / 0.25;
    return QColor::fromRgbF(1.0, 1.0 - position, 0.0);
}

inline QColor sMeterSignalColor(double meterFraction)
{
    struct ColorStop
    {
        double position{0.0};
        QColor color{};
    };
    static const ColorStop kStops[] = {{0.00, QColor(0x00, 0x90, 0x30)}, {0.30, QColor(0x00, 0xc0, 0x40)},
                                       {0.50, QColor(0xd4, 0xc0, 0x00)}, {0.70, QColor(0xdd, 0x14, 0x00)},
                                       {0.85, QColor(0xff, 0x00, 0x00)}, {1.00, QColor(0xff, 0x00, 0x00)}};
    constexpr int kStopCount = int(sizeof(kStops) / sizeof(kStops[0]));
    const double normalized = std::clamp(meterFraction, 0.0, 1.0);
    for (int index = 1; index < kStopCount; ++index)
    {
        if (normalized <= kStops[index].position)
        {
            const ColorStop& lower = kStops[index - 1];
            const ColorStop& upper = kStops[index];
            const double blend = (normalized - lower.position) / (upper.position - lower.position);
            return QColor::fromRgbF(lower.color.redF() + blend * (upper.color.redF() - lower.color.redF()),
                                    lower.color.greenF() + blend * (upper.color.greenF() - lower.color.greenF()),
                                    lower.color.blueF() + blend * (upper.color.blueF() - lower.color.blueF()));
        }
    }
    return kStops[kStopCount - 1].color;
}

namespace Size
{
inline constexpr int DialogContentMargin = 12;
inline constexpr int VfoControlHeight = 46;
inline constexpr int ControlSliderHeight = 24;
inline constexpr int InlineSliderBlockHeight = 42;
inline constexpr int ControlButtonHeight = 40;
inline constexpr int MainWindowMinWidth = 1160;
// Accommodates the enlarged panadapter and waterfall plus its lower shelf
// shadow while retaining the standard spacing around the major control areas.
inline constexpr int MainWindowMinHeight = 922;
inline constexpr int StatusNetworkWidth = 78;
inline constexpr int StatusTxWidth = 80;
inline constexpr int StatusClockWidth = 92;
inline constexpr int StatusSeparatorWidth = 22;
} // namespace Size

inline QString tableStyle(const QString& objectName)
{
    return QStringLiteral(
               "QTableWidget#%1 { background: %2; border: 1px solid %3; color: %4; gridline-color: %3; outline: 0; }"
               "QTableWidget#%1::item { padding: 4px 8px; border: none; }"
               "QTableWidget#%1::item:alternate { background: %5; }"
               "QTableWidget#%1::item:selected { background: %6; color: %7; }"
               "QHeaderView::section { background: %8; border: 0; border-right: 1px solid %3; "
               "border-bottom: 1px solid %3; color: %9; font-weight: bold; padding: 5px 8px; }")
        .arg(objectName, QLatin1String(Color::Field), QLatin1String(Color::Border), QLatin1String(Color::TextField),
             QLatin1String(Color::PanelDark), QLatin1String(Color::AccentDark), QLatin1String(Color::TextBright),
             QLatin1String(Color::MenuBar), QLatin1String(Color::TextStatusSecondary));
}
} // namespace UiTheme
