// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QColor>
#include <QByteArray>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>

#ifdef SDR9700_GPU_PANADAPTER
#include <QRhiWidget>
class QRhiCommandBuffer;
using SpectrumScopeCanvasBase = QRhiWidget;
#else
#include <QWidget>
using SpectrumScopeCanvasBase = QWidget;
#endif

class QPainter;
class QKeyEvent;
class QResizeEvent;

class SpectrumScopeCanvas : public SpectrumScopeCanvasBase
{
    Q_OBJECT
    friend class SpectrumCanvasTest;
#ifdef SDR9700_GPU_PANADAPTER
    friend class GpuPanadapterRenderTest;
#endif

  public:
    explicit SpectrumScopeCanvas(QWidget* parent = nullptr);
    ~SpectrumScopeCanvas() override;

    static int scaleHeight() { return 26; }

    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setVfoFrequency(double freqMhz);
    void setVfoMarkerColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setGridLineColor(const QColor& color);
    void setGridDensity(int density);
    void setInteractionLocked(bool locked);
    void setInvertMouseWheel(bool invert);
    void setFilterWidth(int lowHz, int highHz);
    void updateSpectrum(const QVector<float>& levels, bool outOfRange);
    void clearDisplay();

    int freqToX(double mhz) const;

  signals:
    void frequencyClicked(double freqMhz);
    void wheelStepRequested(int steps);

  protected:
    void paintEvent(QPaintEvent* event) override;
#ifdef SDR9700_GPU_PANADAPTER
    void initialize(QRhiCommandBuffer* commandBuffer) override;
    void render(QRhiCommandBuffer* commandBuffer) override;
    void releaseResources() override;
#endif
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

  private:
    int plotHeight() const;
    static int plotLeftX() { return 0; }
    int plotRightX() const;
    int plotWidthPx() const;
    double xToFreq(int x) const;
    double levelToY(float level, int topY, int h) const;
    double gridLevelToY(float level, int topY, int h) const;
    double sourcePositionForDisplayX(double x, int binCount) const;
    static float interpolatedLevel(const QVector<float>& levels, double sourcePosition);
    static void spatiallySmoothBins(const QVector<float>& bins, QVector<float>* smoothedBins);
    bool isSpectrumClickArea(const QPoint& pos) const;
    void invalidateStaticLayer();
    void ensureStaticLayer();
    void renderStaticLayer(QPainter* painter) const;
    void renderDynamicLayer(QPainter* painter) const;
    void paintRaster(QPainter* painter);
    void buildTraceSamples(QVector<QPointF>* points, QVector<float>* levels) const;
    void scheduleRepaint();
#ifdef SDR9700_GPU_PANADAPTER
    void ensureGpuLayers();
    void rebuildGpuTrace();
    void invalidateGpuOverlay();
    void requestRasterFallback(const QString& reason);
    struct GpuState;
#endif

    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    double m_vfoMhz{145.0};
    QColor m_vfoMarkerColor{0xf5, 0xf7, 0xf8, 230};
    QColor m_backgroundColor{0x08, 0x12, 0x1b};
    QColor m_gridLineColor{0x6f, 0x89, 0x9e};
    float m_minLevel{0.0f};
    // The IC-9700 saturates its scope output at 160. Signal traces use a
    // calibrated non-linear projection; gridLevelToY() deliberately remains
    // linear so the horizontal visual divisions stay evenly spaced.
    float m_maxLevel{160.0f};

    int m_filterLowHz{-1400};
    int m_filterHighHz{1400};
    int m_gridDensity{1};
    bool m_clickPressed{false};
    bool m_interactionLocked{false};
    bool m_invertMouseWheel{false};
    bool m_scopeOutOfRange{false};
    bool m_resetSpectrumSmoothing{true};
    double m_wheelStepAccumulator{0.0};
    QPoint m_clickPressPos;

    QVector<float> m_spectrumBins;
    QVector<float> m_displaySpectrumBins;
    QVector<QPointF> m_tracePointsScratch;
    QVector<float> m_traceLevelsScratch;
    QPolygonF m_tracePolygonScratch;
    QPolygonF m_traceSegmentScratch;
    QPixmap m_staticLayer;
    QSize m_staticLayerSize;
    qreal m_staticLayerDevicePixelRatio{0.0};
    bool m_staticLayerDirty{true};

#ifdef SDR9700_GPU_PANADAPTER
    std::unique_ptr<GpuState> m_gpuState;
    QImage m_gpuOverlayLayer;
    QByteArray m_gpuFillVertices;
    QByteArray m_gpuFeatherVertices;
    QByteArray m_gpuLineVertices;
    QVector<QColor> m_gpuTraceColorsScratch;
    QWidget* m_rasterFallbackOverlay{nullptr};
    bool m_rasterFallbackRequested{false};
    QSize m_gpuTraceSize;
    bool m_gpuBackgroundDirty{true};
    bool m_gpuOverlayDirty{true};
    bool m_gpuOverlayTextureDirty{true};
    bool m_gpuTraceDirty{true};
#endif
};
