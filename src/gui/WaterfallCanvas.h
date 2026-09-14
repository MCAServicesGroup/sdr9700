// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QImage>
#include <QSet>
#include <memory>

#ifdef SDR9700_GPU_PANADAPTER
#include <QRhiWidget>
class QRhiCommandBuffer;
using WaterfallCanvasBase = QRhiWidget;
#else
#include <QWidget>
using WaterfallCanvasBase = QWidget;
#endif

class QPainter;

class WaterfallCanvas : public WaterfallCanvasBase
{
    Q_OBJECT

  public:
    explicit WaterfallCanvas(QWidget* parent = nullptr);
    ~WaterfallCanvas() override;

    void setWaterfallImageSource(const QImage* image);
    void setWaterfallRow(int physicalRow, int firstVisibleRow);

  protected:
    void paintEvent(QPaintEvent* event) override;
#ifdef SDR9700_GPU_PANADAPTER
    void initialize(QRhiCommandBuffer* commandBuffer) override;
    void render(QRhiCommandBuffer* commandBuffer) override;
    void releaseResources() override;
#endif

  private:
    void paintShelf(QPainter* painter) const;
    // WaterfallController owns this image and outlives the canvas within
    // SpectrumScopeDisplay. Keeping a non-owning source avoids QImage
    // copy-on-write detaching the complete waterfall on every rendered row.
    const QImage* m_waterfall{nullptr};
    int m_firstVisibleRow{0};
#ifdef SDR9700_GPU_PANADAPTER
    struct GpuState;
    std::unique_ptr<GpuState> m_gpuState;
    QImage m_gpuShelfLayer;
    QSet<int> m_changedPhysicalRows;
    bool m_fullTextureUploadPending{true};
    bool m_shelfUploadPending{true};
#endif
};
