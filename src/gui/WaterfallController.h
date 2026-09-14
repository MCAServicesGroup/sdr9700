#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QVector>

class QTimer;

class WaterfallController : public QObject
{
    Q_OBJECT

  public:
    explicit WaterfallController(QObject* parent = nullptr);
    const QImage& image() const { return m_waterfall; }
    int firstVisibleRow() const { return m_firstVisibleRow; }

  public slots:
    void setCanvasSize(const QSize& size);
    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setPaused(bool paused);
    void updateSpectrum(const QVector<float>& levels);
    void clearDisplay();

  signals:
    void imageChanged();
    void rowRendered(int physicalRow, int firstVisibleRow);

  private:
    double xToFreq(int x) const;
    int binForFrequency(double mhz, int binCount) const;
    int binForDisplayX(int x, int binCount) const;
    QRgb levelToColor(float level) const;
    void rebuildImage();
    void scheduleRender();
    void renderPendingRow();

    QTimer* m_renderTimer{nullptr};
    QImage m_waterfall;
    QVector<float> m_pendingLevels;
    QSize m_canvasSize;
    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    float m_minLevel{0.0f};
    float m_maxLevel{160.0f};
    bool m_paused{false};
    bool m_hasPendingLevels{false};
    int m_firstVisibleRow{0};
};
