// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QColor;
class SpectrumScopeCanvas;
class QComboBox;
class QScrollBar;
class WaterfallCanvas;
class WaterfallController;

class SpectrumScopeDisplay : public QWidget
{
    Q_OBJECT

  public:
    struct SpanChoice
    {
        quint64 hz{0};
        QString label;
    };

    explicit SpectrumScopeDisplay(QWidget* parent = nullptr);

    void setSpanChoices(const QVector<SpanChoice>& choices);
    void setCurrentSpanHz(quint64 hz);
    QComboBox* spanSelector() const { return m_spanCombo; }
    void setFrequencyRange(double startMhz, double endMhz);
    void setFrequencyPanRange(double startMhz, double endMhz);
    void clearFrequencyPanRange();
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setVfoFrequency(double freqMhz);
    void setVfoMarkerColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setGridLineColor(const QColor& color);
    void setGridDensity(int density);
    void setInteractionLocked(bool locked);
    void setInvertMouseWheel(bool invert);
    void updateSpectrum(const QVector<float>& levels, bool outOfRange);
    void clearDisplay();
    void setFilterWidth(int lowHz, int highHz);
    int freqToX(double mhz) const;

    QSize sizeHint() const override { return {900, 340}; }

  signals:
    void frequencyClicked(double freqMhz);
    void wheelStepRequested(int steps);
    void panCenterRequested(double centerMhz);
    void spanSelected(quint64 hz);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    static int panScrollBarHeight() { return 16; }
    void updateSpanComboGeometry();
    void updatePanScrollBar();
    void updateChildGeometry();

    SpectrumScopeCanvas* m_spectrumScopeCanvas{nullptr};
    QScrollBar* m_panScrollBar{nullptr};
    WaterfallCanvas* m_waterfallCanvas{nullptr};
    WaterfallController* m_waterfallController{nullptr};
    QComboBox* m_spanCombo{nullptr};
    double m_visibleStartMhz{144.0};
    double m_visibleEndMhz{146.0};
    double m_panStartMhz{0.0};
    double m_panEndMhz{0.0};
    bool m_hasPanRange{false};
    bool m_interactionLocked{false};
};
