#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

struct Ax25Frame
{
    QDateTime receivedAt;
    QString source;
    QString destination;
    QString path;
    QString type;
    QString protocol;
    QString payload;
    QByteArray bytes;
};

struct Ax25DecoderStats
{
    int audioLevel{0};
    quint64 candidates{0};
    quint64 decoded{0};
    quint64 fcsFailures{0};
    quint64 malformed{0};
};

Q_DECLARE_METATYPE(Ax25Frame)
Q_DECLARE_METATYPE(Ax25DecoderStats)

class Ax25Decoder
{
  public:
    QVector<Ax25Frame> processPcm16(const QByteArray& pcm, int sampleRate, int channelCount, int receiverChannel);
    QVector<Ax25Frame> processNrziTones(const QVector<bool>& tones);
    void reset();
    const Ax25DecoderStats& stats() const { return m_stats; }

    static quint16 frameCheckSequence(const QByteArray& bytes);
    static Ax25Frame parseFrame(const QByteArray& bytes, bool* valid = nullptr);

  private:
    struct HdlcState
    {
        bool inFrame{false};
        quint8 shiftRegister{0};
        QVector<bool> rawBits;
    };

    struct TimingLane
    {
        bool haveTone{false};
        bool previousTone{false};
        HdlcState hdlc;
    };

    void configure(int sampleRate);
    void acceptBit(HdlcState& state, bool bit, QVector<Ax25Frame>& frames);
    void finishFrame(const QVector<bool>& rawBits, QVector<Ax25Frame>& frames);
    void acceptTone(TimingLane& lane, bool tone, QVector<Ax25Frame>& frames);
    bool isDistinctCandidate(quint64* lastSampleIndex, bool* haveSample);

    int m_sampleRate{0};
    int m_samplesPerSymbol{0};
    quint64 m_sampleIndex{0};
    QVector<float> m_window;
    QVector<double> m_markCos;
    QVector<double> m_markSin;
    QVector<double> m_spaceCos;
    QVector<double> m_spaceSin;
    int m_windowPosition{0};
    int m_windowFill{0};
    QVector<TimingLane> m_lanes;
    Ax25DecoderStats m_stats;
    QByteArray m_lastFrame;
    quint64 m_lastFrameSampleIndex{0};
    bool m_haveFrameSample{false};
    quint64 m_lastRejectedSampleIndex{0};
    bool m_haveRejectedSample{false};
    quint64 m_lastMalformedSampleIndex{0};
    bool m_haveMalformedSample{false};
};
