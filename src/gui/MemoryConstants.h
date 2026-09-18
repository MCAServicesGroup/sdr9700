#pragma once

#include "Types.h"
#include <QtGlobal>

namespace sdr9700::memory
{
constexpr quint16 kRadioMemoryFirstGroup = 1;
constexpr quint16 kRadioMemoryLastGroup = 3;
constexpr quint16 kRadioMemoryFirstChannel = 1;
constexpr quint16 kRadioMemoryLastUserChannel = 99;
constexpr quint16 kRadioMemoryFirstSpecialChannel = 100;
constexpr quint16 kRadioMemoryLastChannel = 107;
constexpr quint16 kRadioMemoryFirstSatelliteChannel = 1;
constexpr quint16 kRadioMemoryLastSatelliteChannel = 99;
constexpr quint16 kRadioMemorySatelliteGroup = 0;
constexpr int kRadioMemorySyncTotal =
    ((kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1)) +
    (kRadioMemoryLastSatelliteChannel - kRadioMemoryFirstSatelliteChannel + 1);
// A full startup sweep is deliberately kept below the rate used by ordinary
// interactive CI-V traffic. The IC-9700 can stop answering CI-V when a newly
// recovered LAN session is immediately loaded with the entire memory sweep.
constexpr int kRadioMemoryRefreshIntervalMs = 75;
constexpr int kRadioMemoryStartupStabilityMs = 1000;
constexpr int kRadioMemoryStartupFallbackMs = 5000;
constexpr int kRadioMemoryStartupMinimumScopeFrames = 3;
constexpr int kRadioMemorySyncReplyGraceMs = 1000;
constexpr int kRadioMemorySyncSafetyMarginMs = 5000;
constexpr int kRadioMemoryOperationSyncMaxAttempts = 3;
// A normal sweep already asks for every radio slot once. Retry only the slots
// that did not answer so a transient loss can recover without multiplying the
// normal 420-command load or keeping startup blocked indefinitely.
constexpr int kRadioMemoryMissingRetryMaxRounds = 2;
constexpr int kRadioMemoryInitialSyncRetryDelayMs = 2000;
constexpr int kRadioMemoryWriteIntervalMs = 100;
constexpr int kRadioMemoryWriteReadbackRetryMs = 500;
constexpr int kRadioMemoryWriteReadbackTimeoutMs = 3000;
constexpr int kRadioMemoryNameMaxChars = 16;
constexpr int kMemoryEditorDialogWidth = 520;
constexpr int kMemoryEditorDialogHeight = 620;
constexpr int kMemoryEditorFieldHeight = 30;
constexpr int kMemoryEditorLabelFieldSpacing = 6;
constexpr int kMemoryFooterTopPadding = 8;
constexpr int kMemoryFooterBottomPadding = 10;
constexpr int kMemoryFooterTextLeftPadding = 6;
constexpr int kMemoryToneCellTextPadding = 8;
constexpr int kMemoryToneTypeRole = Qt::UserRole + 1;
constexpr int kMemoryToneRxRole = Qt::UserRole + 2;
constexpr int kMemoryToneTxRole = Qt::UserRole + 3;
constexpr int kMemoryVerifiedThisSessionRole = Qt::UserRole + 4;
constexpr int kMemoryReadOnlyRole = Qt::UserRole + 5;
constexpr auto kMemoryFileFilter = "SDR9700 Memories (*.csv);;CSV Files (*.csv);;All Files (*)";

enum MemoryToneFamily
{
    MemoryToneOff = 0,
    MemoryToneTone,
    MemoryToneDtcs
};


} // namespace sdr9700::memory
