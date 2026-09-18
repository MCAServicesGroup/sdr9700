#pragma once
#include "AudioHandlerBase.h"

class AudioHandlerQtInput : public AudioHandlerBase
{
    Q_OBJECT

  public:
    explicit AudioHandlerQtInput(QObject* parent = nullptr) : AudioHandlerBase(parent) {}
    ~AudioHandlerQtInput() override { dispose(); }
    QString role() const override { return QStringLiteral("Input"); }

  signals:
    // Transmit-specific publication path. AudioHandlerBase::haveLevels is shared
    // with AudioHandlerQtOutput and remains the receive path untouched; the
    // capture path publishes the typed measurement block here instead, carrying
    // transport health as separate arguments so latency, underrun, and overrun
    // reporting stay intact.
    void haveTxMeter(const sdr9700::audio::TxAudioMeterBlock& block, quint16 configuredLatency, quint16 measuredLatency,
                     bool underrun, bool overrun);

  protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    QAudioFormat getNativeFormat() override;
    bool isFormatSupported(QAudioFormat f) override;

  private:
    QAudioSource* audioInput{nullptr};

    QIODevice* audioDevice{nullptr};
    qsizetype m_bufferReadOffset{0};

  private slots:
    void onReadyRead();
    void onConverted(const audioPacket& audio);
};
