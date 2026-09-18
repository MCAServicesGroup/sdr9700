#include "WaterfallCanvas.h"
#include "LogCategories.h"
#include "UiTheme.h"

#include <QFile>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMatrix4x4>
#include <QPainter>
#include <algorithm>
#include <array>
#include <functional>
#include <utility>

#ifdef SDR9700_GPU_PANADAPTER
#include <rhi/qrhi.h>
#endif

namespace
{
const QColor kWaterfallBg(0x00, 0x24, 0xd8);
constexpr int kControlShelfShadowHeightPx = 8;

#ifdef SDR9700_GPU_PANADAPTER
using TextureVertex = std::array<float, 4>;

class RasterFallbackOverlay final : public QWidget
{
  public:
    RasterFallbackOverlay(QWidget* parent, std::function<void(QPainter*)> paint)
        : QWidget(parent), m_paint(std::move(paint))
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        m_paint(&painter);
    }

  private:
    std::function<void(QPainter*)> m_paint;
};

struct alignas(16) TextureUniforms
{
    float matrix[16]{};
    float rowOffset{0.0f};
    float padding[3]{0.0f, 0.0f, 0.0f};
};

QShader loadShader(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

QRhiGraphicsPipeline::TargetBlend alphaBlend()
{
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
}

const char* rhiBackendName(QRhi::Implementation backend)
{
    switch (backend)
    {
    case QRhi::Vulkan:
        return "Vulkan";
    case QRhi::OpenGLES2:
        return "OpenGL";
    case QRhi::D3D11:
        return "Direct3D11";
    case QRhi::Metal:
        return "Metal";
    case QRhi::D3D12:
        return "Direct3D12";
    case QRhi::Null:
        return "Null";
    }
    return "Unknown";
}
#endif
} // namespace

#ifdef SDR9700_GPU_PANADAPTER
struct WaterfallCanvas::GpuState
{
    QRhi* rhi{nullptr};
    const QRhiTexture* outputTexture{nullptr};
    QRhiRenderPassDescriptor* renderPassDescriptor{nullptr};
    QSize outputSize;
    QSize waterfallTextureSize;
    bool valid{false};
    std::unique_ptr<QRhiBuffer> quadBuffer;
    std::unique_ptr<QRhiBuffer> waterfallUniformBuffer;
    std::unique_ptr<QRhiBuffer> shelfUniformBuffer;
    std::unique_ptr<QRhiTexture> waterfallTexture;
    std::unique_ptr<QRhiTexture> shelfTexture;
    std::unique_ptr<QRhiSampler> waterfallSampler;
    std::unique_ptr<QRhiSampler> shelfSampler;
    std::unique_ptr<QRhiShaderResourceBindings> waterfallBindings;
    std::unique_ptr<QRhiShaderResourceBindings> shelfBindings;
    std::unique_ptr<QRhiGraphicsPipeline> waterfallPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> shelfPipeline;

    void release()
    {
        shelfPipeline.reset();
        waterfallPipeline.reset();
        shelfBindings.reset();
        waterfallBindings.reset();
        shelfSampler.reset();
        waterfallSampler.reset();
        shelfTexture.reset();
        waterfallTexture.reset();
        shelfUniformBuffer.reset();
        waterfallUniformBuffer.reset();
        quadBuffer.reset();
        waterfallTextureSize = {};
        outputSize = {};
        valid = false;
        outputTexture = nullptr;
        renderPassDescriptor = nullptr;
        rhi = nullptr;
    }
};
#endif

WaterfallCanvas::WaterfallCanvas(QWidget* parent) : WaterfallCanvasBase(parent)
{
#ifdef SDR9700_GPU_PANADAPTER
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    {
        setApi(QRhiWidget::Api::Null);
    }
#ifdef Q_OS_MAC
    else
    {
        setApi(QRhiWidget::Api::Metal);
    }
#endif
    m_gpuState = std::make_unique<GpuState>();
    m_changedPhysicalRows.reserve(4);
    connect(this, &QRhiWidget::renderFailed, this,
            [this]() { requestRasterFallback(QStringLiteral("Qt reported that QRhi waterfall rendering failed")); });
#endif
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

WaterfallCanvas::~WaterfallCanvas() = default;

void WaterfallCanvas::setWaterfallImageSource(const QImage* image)
{
    m_waterfall = image;
    m_firstVisibleRow = 0;
#ifdef SDR9700_GPU_PANADAPTER
    m_changedPhysicalRows.clear();
    m_fullTextureUploadPending = true;
#endif
#ifdef SDR9700_GPU_PANADAPTER
    if (m_rasterFallbackOverlay)
    {
        m_rasterFallbackOverlay->update();
        return;
    }
#endif
    update();
}

void WaterfallCanvas::setWaterfallRow(int physicalRow, int firstVisibleRow)
{
    if (!m_waterfall || m_waterfall->isNull())
    {
        return;
    }
    m_firstVisibleRow = qBound(0, firstVisibleRow, m_waterfall->height() - 1);
#ifdef SDR9700_GPU_PANADAPTER
    if (physicalRow >= 0 && physicalRow < m_waterfall->height())
    {
        if (!m_changedPhysicalRows.contains(physicalRow))
        {
            m_changedPhysicalRows.append(physicalRow);
        }
    }
#else
    Q_UNUSED(physicalRow)
#endif
#ifdef SDR9700_GPU_PANADAPTER
    if (m_rasterFallbackOverlay)
    {
        m_rasterFallbackOverlay->update();
        return;
    }
#endif
    update();
}

void WaterfallCanvas::paintShelf(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }
    const int shadowHeight = qMin(height(), kControlShelfShadowHeightPx);
    QLinearGradient shelfShadow(0, 0, 0, shadowHeight);
    shelfShadow.setColorAt(0.0, QColor(0x00, 0x04, 0x08, 220));
    shelfShadow.setColorAt(1.0, QColor(0x00, 0x08, 0x0f, 0));
    painter->fillRect(0, 0, width(), shadowHeight, shelfShadow);
    painter->fillRect(0, 0, width(), 1, UiTheme::Color::ScopeShelfEdge);
}

void WaterfallCanvas::paintEvent(QPaintEvent* event)
{
#ifdef SDR9700_GPU_PANADAPTER
    if (api() != QRhiWidget::Api::Null)
    {
        QRhiWidget::paintEvent(event);
        return;
    }
#endif
    Q_UNUSED(event)

    QPainter painter(this);
    paintRaster(&painter);
}

void WaterfallCanvas::paintRaster(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }

    painter->fillRect(rect(), kWaterfallBg);
    if (m_waterfall && !m_waterfall->isNull())
    {
        const int sourceHeight = qMin(height(), m_waterfall->height());
        const int firstPartHeight = qMin(sourceHeight, m_waterfall->height() - m_firstVisibleRow);
        painter->drawImage(QRect(0, 0, width(), firstPartHeight), *m_waterfall,
                           QRect(0, m_firstVisibleRow, m_waterfall->width(), firstPartHeight));
        if (firstPartHeight < sourceHeight)
        {
            const int secondPartHeight = sourceHeight - firstPartHeight;
            painter->drawImage(QRect(0, firstPartHeight, width(), secondPartHeight), *m_waterfall,
                               QRect(0, 0, m_waterfall->width(), secondPartHeight));
        }
    }
    paintShelf(painter);
}

#ifdef SDR9700_GPU_PANADAPTER
void WaterfallCanvas::requestRasterFallback(const QString& reason)
{
    if (api() == QRhiWidget::Api::Null || m_rasterFallbackRequested)
    {
        return;
    }
    m_rasterFallbackRequested = true;
    qCritical(logWaterfall()).noquote() << reason << "- switching the waterfall to raster rendering";
    if (m_gpuState)
    {
        m_gpuState->valid = false;
    }
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            if (m_rasterFallbackOverlay)
            {
                return;
            }
            m_rasterFallbackOverlay =
                new RasterFallbackOverlay(this, [this](QPainter* painter) { paintRaster(painter); });
            m_rasterFallbackOverlay->setGeometry(rect());
            m_rasterFallbackOverlay->show();
            m_rasterFallbackOverlay->raise();
            m_rasterFallbackOverlay->update();
        },
        Qt::QueuedConnection);
}

void WaterfallCanvas::initialize(QRhiCommandBuffer* commandBuffer)
{
    if (m_rasterFallbackRequested)
    {
        return;
    }
    if (!m_gpuState)
    {
        m_gpuState = std::make_unique<GpuState>();
    }

    QRhi* currentRhi = rhi();
    QRhiRenderTarget* currentTarget = renderTarget();
    const QRhiTexture* currentOutputTexture = colorTexture();
    const QSize outputSize = currentTarget ? currentTarget->pixelSize() : QSize();
    if (!currentRhi || !currentTarget || !currentOutputTexture || outputSize.isEmpty())
    {
        return;
    }
    const bool sameRenderer = m_gpuState->rhi == currentRhi && m_gpuState->outputTexture == currentOutputTexture &&
                              m_gpuState->renderPassDescriptor == currentTarget->renderPassDescriptor();
    if (sameRenderer && m_gpuState->outputSize == outputSize)
    {
        return;
    }
    if (sameRenderer && m_gpuState->valid)
    {
        m_gpuState->shelfTexture->setPixelSize(outputSize);
        if (!m_gpuState->shelfTexture->create())
        {
            requestRasterFallback(QStringLiteral("Could not resize GPU waterfall shelf texture"));
            return;
        }
        m_gpuState->outputSize = outputSize;
        m_shelfUploadPending = true;
        return;
    }

    const bool rhiChanged = m_gpuState->rhi != currentRhi;
    m_gpuState->release();
    GpuState& state = *m_gpuState;
    state.rhi = currentRhi;
    state.outputTexture = currentOutputTexture;
    state.outputSize = outputSize;
    state.renderPassDescriptor = currentTarget->renderPassDescriptor();
    state.waterfallTextureSize = m_waterfall && !m_waterfall->isNull() ? m_waterfall->size() : QSize(1, 1);

    static constexpr TextureVertex kQuadVertices[] = {
        {-1.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
    };
    state.quadBuffer.reset(
        state.rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadVertices)));
    state.waterfallUniformBuffer.reset(
        state.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(TextureUniforms)));
    state.shelfUniformBuffer.reset(
        state.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(TextureUniforms)));
    state.waterfallTexture.reset(state.rhi->newTexture(QRhiTexture::BGRA8, state.waterfallTextureSize));
    state.shelfTexture.reset(state.rhi->newTexture(QRhiTexture::RGBA8, outputSize));
    state.waterfallSampler.reset(state.rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                                       QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    state.shelfSampler.reset(state.rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    const bool resourcesCreated = state.quadBuffer->create() && state.waterfallUniformBuffer->create() &&
                                  state.shelfUniformBuffer->create() && state.waterfallTexture->create() &&
                                  state.shelfTexture->create() && state.waterfallSampler->create() &&
                                  state.shelfSampler->create();
    if (!resourcesCreated)
    {
        state.release();
        requestRasterFallback(QStringLiteral("Could not create GPU waterfall resources"));
        return;
    }

    auto createBindings = [&](QRhiBuffer* uniformBuffer, QRhiTexture* texture, QRhiSampler* sampler)
    {
        std::unique_ptr<QRhiShaderResourceBindings> bindings(state.rhi->newShaderResourceBindings());
        bindings->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniformBuffer),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, sampler)});
        if (!bindings->create())
        {
            return std::unique_ptr<QRhiShaderResourceBindings>();
        }
        return bindings;
    };
    state.waterfallBindings =
        createBindings(state.waterfallUniformBuffer.get(), state.waterfallTexture.get(), state.waterfallSampler.get());
    state.shelfBindings =
        createBindings(state.shelfUniformBuffer.get(), state.shelfTexture.get(), state.shelfSampler.get());
    if (!state.waterfallBindings || !state.shelfBindings)
    {
        state.release();
        requestRasterFallback(QStringLiteral("Could not create GPU waterfall texture bindings"));
        return;
    }

    const QShader vertexShader = loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_texture.vert.qsb"));
    const QShader fragmentShader = loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_texture.frag.qsb"));
    if (!vertexShader.isValid() || !fragmentShader.isValid())
    {
        state.release();
        requestRasterFallback(QStringLiteral("Could not load GPU waterfall shaders"));
        return;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(4 * sizeof(float))});
    layout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                          QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float))});
    auto createPipeline = [&](QRhiShaderResourceBindings* bindings, bool blending)
    {
        std::unique_ptr<QRhiGraphicsPipeline> pipeline(state.rhi->newGraphicsPipeline());
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
        pipeline->setVertexInputLayout(layout);
        pipeline->setShaderResourceBindings(bindings);
        pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline->setSampleCount(currentTarget->sampleCount());
        pipeline->setRenderPassDescriptor(state.renderPassDescriptor);
        if (blending)
        {
            pipeline->setTargetBlends({alphaBlend()});
        }
        if (!pipeline->create())
        {
            return std::unique_ptr<QRhiGraphicsPipeline>();
        }
        return pipeline;
    };
    state.waterfallPipeline = createPipeline(state.waterfallBindings.get(), false);
    state.shelfPipeline = createPipeline(state.shelfBindings.get(), true);
    if (!state.waterfallPipeline || !state.shelfPipeline)
    {
        state.release();
        requestRasterFallback(QStringLiteral("Could not create GPU waterfall pipelines"));
        return;
    }

    QRhiResourceUpdateBatch* updates = state.rhi->nextResourceUpdateBatch();
    updates->uploadStaticBuffer(state.quadBuffer.get(), kQuadVertices);
    commandBuffer->resourceUpdate(updates);
    state.valid = true;
    m_fullTextureUploadPending = true;
    m_shelfUploadPending = true;
    if (rhiChanged)
    {
        const QRhiDriverInfo driver = state.rhi->driverInfo();
        qInfo(logWaterfall()).noquote().nospace()
            << "GPU waterfall initialized backend=" << rhiBackendName(state.rhi->backend())
            << " device=" << driver.deviceName;
    }
}

void WaterfallCanvas::render(QRhiCommandBuffer* commandBuffer)
{
    if (m_rasterFallbackRequested)
    {
        return;
    }
    QRhiRenderTarget* currentTarget = renderTarget();
    const QRhiTexture* currentOutputTexture = colorTexture();
    if (!m_gpuState || !currentTarget || !currentOutputTexture)
    {
        return;
    }
    if (m_gpuState->rhi != rhi() || m_gpuState->outputTexture != currentOutputTexture ||
        m_gpuState->renderPassDescriptor != currentTarget->renderPassDescriptor() ||
        m_gpuState->outputSize != currentTarget->pixelSize())
    {
        initialize(commandBuffer);
    }
    if (!m_gpuState->valid)
    {
        return;
    }
    GpuState& state = *m_gpuState;
    if (m_waterfall && !m_waterfall->isNull() && state.waterfallTextureSize != m_waterfall->size())
    {
        const QSize requestedSize = m_waterfall->size();
        state.waterfallTexture->setPixelSize(requestedSize);
        if (!state.waterfallTexture->create())
        {
            requestRasterFallback(QStringLiteral("Could not resize GPU waterfall texture"));
            return;
        }
        state.waterfallTextureSize = requestedSize;
        m_fullTextureUploadPending = true;
    }

    const qreal devicePixelRatio = devicePixelRatioF();
    if (m_shelfUploadPending || m_gpuShelfLayer.size() != state.outputSize)
    {
        if (m_gpuShelfLayer.size() != state.outputSize || m_gpuShelfLayer.format() != QImage::Format_RGBA8888)
        {
            m_gpuShelfLayer = QImage(state.outputSize, QImage::Format_RGBA8888);
        }
        m_gpuShelfLayer.setDevicePixelRatio(devicePixelRatio);
        m_gpuShelfLayer.fill(Qt::transparent);
        QPainter painter(&m_gpuShelfLayer);
        paintShelf(&painter);
        m_shelfUploadPending = true;
    }

    TextureUniforms waterfallUniforms;
    TextureUniforms shelfUniforms;
    const QMatrix4x4 matrix = state.rhi->clipSpaceCorrMatrix();
    std::copy(matrix.constData(), matrix.constData() + 16, waterfallUniforms.matrix);
    std::copy(matrix.constData(), matrix.constData() + 16, shelfUniforms.matrix);
    if (m_waterfall && !m_waterfall->isNull())
    {
        waterfallUniforms.rowOffset = float(m_firstVisibleRow) / float(m_waterfall->height());
    }

    QRhiResourceUpdateBatch* updates = state.rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(state.waterfallUniformBuffer.get(), 0, sizeof(waterfallUniforms), &waterfallUniforms);
    updates->updateDynamicBuffer(state.shelfUniformBuffer.get(), 0, sizeof(shelfUniforms), &shelfUniforms);
    if (m_waterfall && !m_waterfall->isNull())
    {
        if (m_fullTextureUploadPending)
        {
            updates->uploadTexture(state.waterfallTexture.get(), *m_waterfall);
        }
        else
        {
            QVector<QRhiTextureUploadEntry> entries;
            entries.reserve(m_changedPhysicalRows.size());
            for (int physicalRow : std::as_const(m_changedPhysicalRows))
            {
                const QImage rowImage(m_waterfall->constScanLine(physicalRow), m_waterfall->width(), 1,
                                      m_waterfall->bytesPerLine(), QImage::Format_RGB32);
                QRhiTextureSubresourceUploadDescription rowUpload(rowImage);
                rowUpload.setSourceSize(QSize(m_waterfall->width(), 1));
                rowUpload.setDestinationTopLeft(QPoint(0, physicalRow));
                entries.append(QRhiTextureUploadEntry(0, 0, rowUpload));
            }
            if (!entries.isEmpty())
            {
                QRhiTextureUploadDescription upload;
                upload.setEntries(entries.cbegin(), entries.cend());
                updates->uploadTexture(state.waterfallTexture.get(), upload);
            }
        }
    }
    if (m_shelfUploadPending)
    {
        updates->uploadTexture(state.shelfTexture.get(), m_gpuShelfLayer);
    }
    m_fullTextureUploadPending = false;
    m_shelfUploadPending = false;
    m_changedPhysicalRows.clear();

    commandBuffer->beginPass(renderTarget(), kWaterfallBg, {1.0f, 0}, updates);
    const QRhiCommandBuffer::VertexInput binding(state.quadBuffer.get(), 0);
    if (m_waterfall && !m_waterfall->isNull())
    {
        commandBuffer->setGraphicsPipeline(state.waterfallPipeline.get());
        commandBuffer->setViewport(QRhiViewport(0, 0, state.outputSize.width(), state.outputSize.height()));
        commandBuffer->setShaderResources(state.waterfallBindings.get());
        commandBuffer->setVertexInput(0, 1, &binding);
        commandBuffer->draw(4);
    }
    commandBuffer->setGraphicsPipeline(state.shelfPipeline.get());
    commandBuffer->setViewport(QRhiViewport(0, 0, state.outputSize.width(), state.outputSize.height()));
    commandBuffer->setShaderResources(state.shelfBindings.get());
    commandBuffer->setVertexInput(0, 1, &binding);
    commandBuffer->draw(4);
    commandBuffer->endPass();
}

bool WaterfallCanvas::gpuResourcesActiveForTest() const
{
    return m_gpuState && m_gpuState->valid && !m_rasterFallbackRequested;
}

QString WaterfallCanvas::gpuBackendNameForTest() const
{
    return m_gpuState && m_gpuState->rhi ? QString::fromLatin1(rhiBackendName(m_gpuState->rhi->backend())) : QString();
}

void WaterfallCanvas::releaseResources()
{
    if (m_gpuState)
    {
        m_gpuState->release();
    }
}
#endif

void WaterfallCanvas::resizeEvent(QResizeEvent* ev)
{
    WaterfallCanvasBase::resizeEvent(ev);
#ifdef SDR9700_GPU_PANADAPTER
    if (m_rasterFallbackOverlay)
    {
        m_rasterFallbackOverlay->setGeometry(rect());
    }
#endif
}
