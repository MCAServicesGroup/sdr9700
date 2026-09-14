#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QVector>

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
    void invalidateBinMap();
    void ensureBinMap(int binCount);
    void rebuildImage();
    void renderRow(const QVector<float>& levels);

    QImage m_waterfall;
    QVector<int> m_binMap;
    int m_binMapBinCount{0};
    QSize m_canvasSize;
    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    float m_minLevel{0.0f};
    float m_maxLevel{160.0f};
    bool m_paused{false};
    int m_firstVisibleRow{0};
};
