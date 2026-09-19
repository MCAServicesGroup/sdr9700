#pragma once

#include "Ax25Decoder.h"
#include "UtilityWindow.h"

#include <QThread>
#include <QElapsedTimer>

class QLabel;
class QHideEvent;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class Ax25DecoderWorker : public QObject
{
    Q_OBJECT

  public slots:
    void processAudio(const QByteArray& pcm, int sampleRate, int channelCount, int receiverChannel);
    void reset();

  signals:
    void framesDecoded(const QVector<Ax25Frame>& frames);
    void statsChanged(const Ax25DecoderStats& stats);

  private:
    Ax25Decoder m_decoder;
    int m_receiverChannel{-1};
};

class DataDecoderDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit DataDecoderDialog(QWidget* parent = nullptr);
    ~DataDecoderDialog() override;

  public slots:
    void processAudio(const QByteArray& pcm, int sampleRate, int channelCount, int receiverChannel);

  protected:
    void hideEvent(QHideEvent* event) override;

  signals:
    void audioReceived(const QByteArray& pcm, int sampleRate, int channelCount, int receiverChannel);
    void resetDecoder();

  private slots:
    void appendFrames(const QVector<Ax25Frame>& frames);
    void updateStats(const Ax25DecoderStats& stats);
    void exportPackets();

  private:
    QString exportText() const;

    QThread m_decoderThread;
    Ax25DecoderWorker* m_worker{nullptr};
    QLabel* m_statusIndicator{nullptr};
    QLabel* m_statusText{nullptr};
    QLabel* m_candidatesValue{nullptr};
    QLabel* m_decodedValue{nullptr};
    QLabel* m_rejectedValue{nullptr};
    QLabel* m_successValue{nullptr};
    QTableWidget* m_packetTable{nullptr};
    QPlainTextEdit* m_packetDetails{nullptr};
    QPushButton* m_pauseButton{nullptr};
    bool m_paused{false};
    QVector<Ax25Frame> m_pausedFrames;
    QElapsedTimer m_lastDecode;
    quint64 m_lastDecodedCount{0};
};
