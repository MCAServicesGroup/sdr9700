#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QFontMetrics>
#include <QTimer>
#include <QWidget>

class VfoSMeter : public QWidget
{
    Q_OBJECT

  public:
    explicit VfoSMeter(QWidget* parent = nullptr);

    void setRawValue(int value);
    void setTransmitPowerMode(bool enabled);
    void setPowerWatts(double watts);
    void setMaxPowerWatts(double watts);

  protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void advanceSignalDisplay();
    void refreshPaintFonts();

    int m_rawValue{0};
    double m_displayRawValue{0.0};
    double m_powerWatts{0.0};
    double m_maxPowerWatts{100.0};
    bool m_transmitPowerMode{false};
    QFont m_scaleFont;
    QFont m_readoutFont;
    QFontMetrics m_scaleMetrics{QFont()};
    QTimer m_signalAnimationTimer;
    QElapsedTimer m_signalAnimationElapsed;
};
