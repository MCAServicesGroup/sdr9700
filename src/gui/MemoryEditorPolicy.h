#pragma once

#include "MainWindowHelpers.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"

#include <algorithm>
#include <QSize>

namespace sdr9700::memory
{
using namespace sdr9700::ui::main_window;
inline MemoryToneFamily memoryToneFamilyForMode(rptAccessTxRx_t mode)
{
    if (isDtcsToneMode(mode))
    {
        return MemoryToneDtcs;
    }
    if (mode == ratrTN || mode == ratrNT || mode == ratrTT || mode == ratrTD)
    {
        return MemoryToneTone;
    }
    return MemoryToneOff;
}


inline bool modeSupportsMemoryOffset(int mode)
{
    return mode == modeFM || mode == modeDV || mode == modeDD;
}

inline QSize memoryEditorDialogSize(const QSize& availableSize)
{
    constexpr int kScreenMargin = 24;
    return QSize(std::min(kMemoryEditorDialogWidth, std::max(1, availableSize.width() - kScreenMargin)),
                 std::min(kMemoryEditorDialogHeight, std::max(1, availableSize.height() - kScreenMargin)));
}

constexpr int radioMemorySyncTimeoutMs()
{
    return (kRadioMemorySyncTotal * kRadioMemoryRefreshIntervalMs) + kRadioMemorySyncReplyGraceMs +
           kRadioMemorySyncSafetyMarginMs;
}

} // namespace sdr9700::memory
