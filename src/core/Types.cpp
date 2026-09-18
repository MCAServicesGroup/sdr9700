#include "Types.h"

const QString meterString[meterUnknown + 1]{"None",    "S-Meter", "Center", "SWR",    "Power",    "ALC",   "Comp",
                                            "Voltage", "Current", "RX dB",  "TX Mod", "RX Audio", "Audio", "Latency",
                                            "dBu",     "dBu EMF", "dBm",    "Sub S",  ""};

// Any changes to these strings WILL break compiled radio capability definitions; add new entries at the end.
// Missing commas concatenate adjacent strings.
const QString funcString[funcLastFunc]{
    "None",
    // CI-V group 00-0f: VFO information.
    "+<CMD00-0f VFO>", "Freq (TRX)", "Mode (TRX)", "Band Edge Freq", "Freq Get", "Mode Get", "Freq Set", "Mode Set",
    "VFO Swap A/B", "VFO Swap M/S", "VFO Equal AB", "VFO Equal MS", "VFO Dual Watch Off", "VFO Dual Watch On",
    "VFO Dual Watch", "VFO Main Select", "VFO Sub Select", "VFO A Select", "VFO B Select", "VFO Main/Sub Band",
    "Memory Mode", "Memory Write", "Memory to VFO", "Memory Clear", "Read Freq Offset", "Send Freq Offset", "Scanning",
    "VFO Mode Select", "Split/Duplex",

    // CI-V group 10-13: basic settings.
    "+<CMD10-13 Basic>", "Tuning Step", "Attenuator Status", "Antenna", "Speech",

    // CI-V group 14: level controls.
    "+<CMD14 Levels>", "AF Gain", "RF Gain", "Squelch", "APF Level", "NR Level", "IF Shift", "PBT Inner", "PBT Outer",
    "CW Pitch", "RF Power", "Key Speed", "Notch Filter", "Compressor Level", "Break-In Delay", "NB Level",
    "DIGI-SEL Shift", "Drive Gain", "Monitor Gain", "Vox Gain", "Anti-Vox Gain", "Backlight Level",

    // CI-V group 15: meter readings.
    "+<CMD15 - Meters>", "S Meter Sql Status", "S Meter", "Absolute Meter", "Meter Type", "Center Meter",
    "Various Squelch", "Power Meter", "SWR Meter", "ALC Meter", "Comp Meter", "Vd Meter", "Id Meter",

    "+<CMD16 - En/Dis>",
    // CI-V group 16: function toggles.
    "Preamp Status", "AGC Status", "Noise Blanker", "Audio Peak Filter", "Noise Reduction", "Auto Notch",
    "Repeater Tone", "Repeater TSQL", "Repeater DTCS", "Repeater CSQL", "Compressor Status", "Monitor Status",
    "Vox Status", "Break-In Status", "Manual Notch", "DIGI-Sel Status", "Twin Peak Filter", "Dial Lock Status",
    "RX Antenna", "Manual Notch Width", "SSB TX Bandwidth", "Main/Sub Tracking", "Satellite Mode", "DSQL Setting",
    "Tone Squelch Type", "IP Plus Status", "Roofing Filter", "Filter Shape",

    // CI-V group 17-19: CW, power, and transceiver ID.
    "+<CMD17-19>", "Send CW", "Power Control", "Transceiver ID",

    // CI-V group 1A00-1A04: memory and filter settings.
    "+CMD1A00-1A04", "Memory Contents", "Band Stacking Reg", "Filter Width", "AGC Time Constant",

    // CI-V group 1A0500: set-mode options.
    "+<CMD1A0500>", "SSB RX HPFLPF", "SSB RX Bass", "SSB RX Treble", "AM RX HPFLPF", "AM RX Bass", "AM RX Treble",
    "FM RX HPFLPF", "FM RX Bass", "FM RX Treble", "CW RX HPFLPF", "RTTY RX HPFLPF", "SSB TX Bass", "SSB TX Treble",
    "AM TX Bass", "AM TX Treble", "FM TX Bass", "FM TX Treble", "Beep Level", "Beep Level Limit", "Beep Confirmation",
    "Band Edge Beep", "Beep Main Band", "Beep Sub Band",

    "RF SQL Control", "TX Delay HF", "TX Delay 50m", "Timeout Timer", "Timeout C-IV",

    "Quick Dual Watch", "Quick Split", "Auto Repeater Mode", "Transverter Function", "Transverter Offset",
    "Lock Function", "REF Adjust", "REF Adjust Fine", "ACC1 Mod Level", "ACC2 Mod Level", "USB Mod Level",
    "LAN Mod Level", "SPDIF Mod Level", "Data Off Mod Input", "DATA1 Mod Input", "DATA2 Mod Input", "DATA3 Mod Input",
    "CIV Transceive", "System Time", "System Date", "UTC Offset", "CLOCK2 Setting", "CLOCK2 UTC Offset", "CLOCK 2 Name",
    "Dash Ratio", "Scanning Speed", "Scanning Resume", "Recorder Mode", "Recorder TX", "Recorder RX", "Recorder Split",
    "Recorder PTT Auto", "Recorder Pre Rec", "RX Ant Connector", "Antenna Select Mode", "NB Depth", "NB Width",
    "VOX Delay", "VOX Voice Delay", "APF Type", "APF Type Level", "PSK Tone", "RTTY Mark Tone", "Tone Frequency",
    "TSQL Frequency", "DTCS Code/Polarity", "CSQL Code", "Transmit Freq Mon", "Read User TX Freqs", "CI-V Output (ANT)",
    "Voice TX Level", "Main/Sub Prefix", "AFC Function", "GPS TX Mode", "Satellite Memory", "GPS Position",
    "Memory Group",

    // CI-V group 1A0501: spectrum display settings.
    "+<CMD1A0501>", "Monitor Signal Width", "Scope Averaging", "Spectrum Fill Type", "Spectrum Fill Color",
    "Spectrum Line Color", "Spectrum Peak Color", "Waterfall Set", "Waterfall Speed", "Waterfall Height",
    "Waterfall Peak Level", "Marker Auto Hide",

    // CI-V group 1A0502: scope edge settings.
    "+<CMD1A0502>",

    "Scope Edge1 1.6MHz", "Scope Edge2 1.6MHz", "Scope Edge3 1.6MHz", "Scope Edge4 1.6MHz", "Scope Edge1 2MHz",
    "Scope Edge2 2MHz", "Scope Edge3 2MHz", "Scope Edge4 2MHz", "Scope Edge1 6MHz", "Scope Edge2 6MHz",
    "Scope Edge3 6MHz", "Scope Edge4 6MHz", "Scope Edge1 8MHz", "Scope Edge2 8MHz", "Scope Edge3 8MHz",
    "Scope Edge4 8MHz", "Scope Edge1 11MHz", "Scope Edge2 11MHz", "Scope Edge3 11MHz", "Scope Edge4 11MHz",
    "Scope Edge1 15MHz", "Scope Edge2 15MHz", "Scope Edge3 15MHz", "Scope Edge4 15MHz", "Scope Edge1 20MHz",
    "Scope Edge2 20MHz", "Scope Edge3 20MHz", "Scope Edge4 20MHz", "Scope Edge1 22MHz", "Scope Edge2 22MHz",
    "Scope Edge3 22MHz", "Scope Edge4 22MHz", "Scope Edge1 26MHz", "Scope Edge2 26MHz", "Scope Edge3 26MHz",
    "Scope Edge4 26MHz", "Scope Edge1 30MHz", "Scope Edge2 30MHz", "Scope Edge3 30MHz", "Scope Edge4 30MHz",
    "Scope Edge1 45MHz", "Scope Edge2 45MHz", "Scope Edge3 45MHz", "Scope Edge4 45MHz", "Scope Edge1 60MHz",
    "Scope Edge2 60MHz", "Scope Edge3 60MHz", "Scope Edge4 60MHz", "Scope Edge1 74MHz", "Scope Edge2 74MHz",
    "Scope Edge3 74MHz", "Scope Edge4 74MHz", "Scope Edge1 144MHz", "Scope Edge2 144MHz", "Scope Edge3 144MHz",
    "Scope Edge4 144MHz", "Scope Edge1 430MHz", "Scope Edge2 430MHz", "Scope Edge3 430MHz", "Scope Edge4 430MHz",
    "Scope Edge1 1200MHz", "Scope Edge2 1200MHz", "Scope Edge3 1200MHz", "Scope Edge4 1200MHz", "Scope Edge1 2400MHz",
    "Scope Edge2 2400MHz", "Scope Edge3 2400MHz", "Scope Edge4 2400MHz", "Scope Edge1 5600MHz", "Scope Edge2 5600MHz",
    "Scope Edge3 5600MHz", "Scope Edge4 5600MHz", "Scope Edge1 10GHz", "Scope Edge2 10GHz", "Scope Edge3 10GHz",
    "Scope Edge4 10GHz",

    // CI-V group 1A06-1A0A: data mode and AF mute.
    "+<CMD1A06-1A0A>", "Data Mode Filter", "AF Mute Status", "Overflow Status",

    // CI-V group 1C: transceiver status.
    "+  <CMD1C>", "Transceiver Status", "Tuner/ATU Status", "XFC Status", "TX Freq",

    // CI-V group 1E: TX frequency limits.
    "+<CMD1E>", "Available TX Freq", "Read TX Band Edge", "Read Num User TX Band", "User TX Band Edge Freq",

    // CI-V group 21: RIT state.
    "+<CMD21>", "RIT Frequency", "RIT Status", "RIT TX Status",

    // CI-V group 25/26: selected and unselected VFO state.
    "+<CMD25/26 Freq>", "Selected Freq", "Unselected Freq", "Selected Mode", "Unselected Mode", "RX Frequency",
    "RX Mode",

    // CI-V group 27: scope control.
    "+<CMD27 - Scope>", "Scope Wave Data", "Scope On/Off", "Scope Data Output", "Scope Main/Sub", "Scope Single/Dual",
    "Scope Mode", "Scope Span", "Scope Edge", "Scope Hold", "Scope Ref", "Scope Speed", "Scope VBW", "Scope RBW",
    "Scope Center Freq", "Scope During TX", "Scope Center Type", "Scope Fixed Edge Freq",

    // CI-V group 28: voice transmit.
    "+<CMD28 Voice TX>", "Voice TX",

    "+<Response Codes>", "Command Error FA", "Command OK FB",

    // SDR9700 internal function entries.
    "-Select VFO", "-Separator", "-LCD Waterfall", "-LCD Spectrum", "-LCD Nothing", "-Page Up", "-Page Down",
    "-VFO Frequency", "-VFO Mode", "-Radio Control Function", "-Radio Control Level", "-Radio Control Param",
    "-RX Audio Data", "-TX Audio Data"};
