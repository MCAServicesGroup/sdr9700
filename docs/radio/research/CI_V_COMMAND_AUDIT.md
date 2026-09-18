# Icom IC-9700 CI-V Command Audit

Source: Icom IC-9700 CI-V Reference Guide, version shown by the PDF metadata as modified 2023-03-30. The repository research copy is `resources/manuals/IC_9700_ENG_CI_V_4.pdf`.

This audit compares the manual command table against SDR9700's compiled IC-9700 command table in `src/radio/RadioCapabilities.h` and command translations in `src/radio/Commander.cpp`. The **Implemented** column means the exact CI-V command prefix, or at least one command under that family, has an active encoding or decoding path. `Partial` means SDR9700 implements only some subcommands/registers under that manual command family. This is an implementation inventory, not a hardware-verification record; radio behavior still requires logs, captures, or direct testing.

Extraction notes:

- Function commands are grouped by CI-V command family so subcommands keep their manual context.
- Radio settings are listed at register level, primarily from the manual's `1A 05` menu/configuration block.
- A `Yes` or `Partial` row does not guarantee that every payload form is fully encoded, decoded, exposed in the GUI, or hardware-verified.
- The PDF table's footnote asterisks are omitted from command cells.

## Function Commands

| CI-V command | Data | Description | Implemented | SDR9700 function(s) |
| --- | --- | --- | --- | --- |
| `00` | See p. 13 | Send frequency data by transceive | Yes | `funcFreqTR` |
| `01` | See p. 13 | Send mode data by transceive | Yes | `funcModeTR` |
| `02` | See p. 13 | Read band-edge frequencies | Yes | `funcBandEdgeFreq` |
| `03` | See p. 13 | Read operating frequency | Yes | `funcFreqGet` |
| `04` | See p. 13 | Read operating mode | Yes | `funcModeGet` |
| `05` | See p. 13 | Set operating frequency | Yes | `funcFreqSet` |
| `06` | See p. 13 | Set operating mode | Yes | `funcModeSet` |
| `07` | subcommands | VFO mode, VFO A/B, equalize, main/sub swap, main/sub select, and main/sub band selection | Yes | `funcVFOModeSelect` |
| `07 00` | - | Select VFO A | Yes | `funcVFOASelect` |
| `07 01` | - | Select VFO B | Yes | `funcVFOBSelect` |
| `07 A0` | - | Equalize VFO A and VFO B | Yes | `funcVFOEqualAB` |
| `07 B0` | - | Exchange MAIN and SUB bands | Yes | `funcVFOSwapMS` |
| `07 D0` | - | Select main band | Yes | `funcVFOMainSelect` |
| `07 D1` | - | Select sub band | Yes | `funcVFOSubSelect` |
| `07 D2` | 00 or 01 | Send/read main/sub band selection | Yes | `funcVFOBandMS` |
| `08` | channel data | Memory mode and memory/channel selection | Yes | `funcMemoryMode` |
| `09` | - | Memory write | Yes | `funcMemoryWrite` |
| `0A` | - | Memory copy to VFO | Yes | `funcMemoryToVFO` |
| `0B` | - | Memory clear | Yes | `funcMemoryClear` |
| `0C` | See p. 13 | Read frequency offset | Yes | `funcReadFreqOffset` |
| `0D` | See p. 13 | Send frequency offset | Yes | `funcSendFreqOffset` |
| `0E` | subcommands | Scan cancel/start, Delta-F scan span, select-channel marking, and scan resume | Yes | `funcScanning` |
| `0F` | subcommands | Read/set split, simplex, DUP-, DUP+, and DD repeater simplex operation | Yes | `funcSplitStatus` |
| `10` | 00 to 11 | Send/read tuning step | Yes | `funcTuningStep` |
| `11` | 00 or 10 | Send/read attenuator setting | Yes | `funcAttenuator` |
| `13` | 00 to 02 | Speech synthesizer readout commands | Yes | `funcSpeech` |
| `14` | subcommands | Send/read operator levels: AF, RF, squelch, NR, PBT, CW pitch, RF power, MIC gain, key speed, notch, compressor, break-in delay, NB level, monitor, VOX, anti-VOX, and LCD backlight | Partial | `funcAfGain`, `funcRfGain`, `funcSquelch`, `funcNRLevel`, `funcPBTInner`, `funcPBTOuter`, plus 11 more |
| `15` | subcommands | Read squelch, S-meter, OVF, power, SWR, ALC, COMP, Vd, and Id meters/statuses | Partial | `funcSMeterSqlStatus`, `funcSMeter`, `funcVariousSql`, `funcOverflowStatus`, `funcPowerMeter`, `funcSWRMeter`, plus 4 more |
| `16` | subcommands | Send/read runtime functions: preamp, AGC time constant, NB, NR, ANF, tone/DTCS, compressor, monitor, VOX, break-in, notch, filter shape, dualwatch, satellite mode, DSQL/CSQL, GPS TX mode, tone squelch type, and IP Plus | Partial | `funcPreamp`, `funcAGCTimeConstant`, `funcNoiseBlanker`, `funcNoiseReduction`, `funcAutoNotch`, `funcRepeaterTone`, plus 18 more |
| `17` | See p. 10 | Send CW messages | Yes | `funcSendCW` |
| `18` | 00 or 01 | Turn transceiver off/on | Yes | `funcPowerControl` |
| `19 00` | - | Read transceiver ID | Yes | `funcTransceiverId` |
| `1A 00` | See pp. 14, 15 | Send/read memory contents | Yes | `funcMemoryContents` |
| `1A 01` | See p. 15 | Send/read band-stacking register contents | Yes | `funcBandStackReg` |
| `1A 02` | See pp. 15, 16 | Send/read memory keyer contents | No | - |
| `1A 03` | See p. 16 | Send/read selected IF filter width | Yes | `funcFilterWidth` |
| `1A 04` | See p. 16 | Send/read selected AGC time constant | No | - |
| `1A 05` | registers | Menu/configuration register block; see Radio Settings table | Partial | `funcQuickSplit`, `funcREFAdjust`, `funcREFAdjustFine`, `funcACCAModLevel`, `funcUSBModLevel`, `funcLANModLevel`, plus 7 more |
| `1A 06` | See p. 18 | Data mode with filter set | Yes | `funcDataModeWithFilter` |
| `1A 07` | See p. 19 | Satellite memory contents | Yes | `funcSatelliteMemory` |
| `1A 08` | 00 or 01 | NTP server access | No | - |
| `1A 09` | 00 to 02 | Read NTP server access result | No | - |
| `1A 0A` | 00 or 01 | Read OVF indicator status | No | - |
| `1A 0B` | 00 to 02 | Picture TX | No | - |
| `1A 0C` | 00 or 01 | Send/read RPT MONI setting | No | - |
| `1B` | subcommands | Repeater tone frequency, TSQL tone frequency, DTCS code/polarity, and CSQL code | Partial | `funcToneFreq`, `funcTSQLFreq`, `funcDTCSCode`, `funcCSQLCode` |
| `1C` | subcommands | Transceiver TX/RX status, XFC, and transmit frequency read | Partial | `funcTransceiverStatus`, `funcXFCStatus` |
| `1E` | subcommands | Read/set TX frequency band edge information | No | - |
| `1F` | subcommands | DV My Station callsign, UR/R1/R2, and TX message settings | No | - |
| `20` | subcommands | DV auto RX callsign/message/status/GPS/D-PRS output and status commands | No | - |
| `21` | 00 or 01 | RIT frequency and RIT on/off setting | Partial | `funcRitFreq`, `funcRitStatus` |
| `22` | subcommands | DV TX/RX data and DV fast-data setup commands | No | - |
| `23` | 00 to 02 | GPS position/status, GPS select, and manual position | Partial | `funcGPSPosition` |
| `24` | 00/01 | TX output power setting and transceive output | No | - |
| `25` | See p. 24 | Set selected or unselected VFO frequency | Partial | `funcSelectedFreq`, `funcUnselectedFreq` |
| `26` | See p. 24 | Set selected or unselected VFO mode and filter | Partial | `funcSelectedMode`, `funcUnselectedMode` |
| `27` | subcommands | Scope waveform, on/off, data output, main/sub, mode, span, edge, hold, reference, sweep speed, during-TX, center type, VBW, fixed edges, and marker position | Partial | `funcScopeWaveData`, `funcScopeOnOff`, `funcScopeDataOutput`, `funcScopeMainSub`, `funcScopeMode`, `funcScopeSpan`, plus 8 more |
| `28 00` | 00 to 08 | Voice TX memory | Yes | `funcVoiceTX` |

## Radio Settings

| CI-V command | Data | Description | Implemented | SDR9700 function | Manual page |
| --- | --- | --- | --- | --- | --- |
| `1A 05 0001` | See p. 16 | SET > Tone Control/TBW > RX > Send/read SSB RX HPF/LPF settings | No | - | 6 |
| `1A 05 0002` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read SSB RX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0003` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read SSB RX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0004` | See p. 16 | SET > Tone Control/TBW > RX > Send/read AM RX HPF/LPF settings | No | - | 6 |
| `1A 05 0005` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read AM RX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0006` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read AM RX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0007` | See p. 16 | SET > Tone Control/TBW > RX > Send/read FM RX HPF/LPF settings | No | - | 6 |
| `1A 05 0008` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read FM RX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0009` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read FM RX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0010` | See p. 16 | SET > Tone Control/TBW > RX > Send/read DV RX HPF/LPF settings | No | - | 6 |
| `1A 05 0011` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read DV RX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0012` | 00 to 10 | SET > Tone Control/TBW > RX > Send/read Auto DV RX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0013` | See p. 16 | SET > Tone Control/TBW > RX > Send/read CW RX HPF/LPF settings | No | - | 6 |
| `1A 05 0014` | See p. 16 | SET > Tone Control/TBW > RX > Send/read RTTY RX HPF/LPF settings | No | - | 6 |
| `1A 05 0015` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read SSB TX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0016` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read SSB TX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0017` | See p. 16 | SET > Tone Control/TBW > TX > Send/read SSB TX bandwidth for wide | No | - | 6 |
| `1A 05 0018` | See p. 16 | SET > Tone Control/TBW > TX > Send/read SSB TX bandwidth for mid | No | - | 6 |
| `1A 05 0019` | See p. 16 | SET > Tone Control/TBW > TX > Send/read SSB TX bandwidth for narrow | No | - | 6 |
| `1A 05 0020` | See p. 16 | SET > Tone Control/TBW > TX > SSB-D TX passband width | No | - | 6 |
| `1A 05 0021` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read AM TX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0022` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read AM TX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0023` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read FM TX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0024` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read FM TX Tone (Treble) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0025` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read DV TX Tone (Bass) level (00=-5 to 10=+5) | No | - | 6 |
| `1A 05 0026` | 00 to 10 | SET > Tone Control/TBW > TX > Send/read DV TX Tone (Treble) level | No | - | 6 |
| `1A 05 0027` | 0000 ~ 0255 | SET > Function > Beep Level (0000=Minimum to 0255=Maximum) | No | - | 7 |
| `1A 05 0028` | 00 or 01 | SET > Function > Beep Level Limit (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0029` | 00 or 01 | SET > Function > Beep (Confirmation) (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0030` | 00 or 01 | SET > Function > Band Edge Beep (00=OFF, 01=ON) (ON = Beep sounds with a default amateur band) | No | - | 7 |
| `1A 05 0031` | 0050 ~ 0200 | SET > Function > Beep Sound (MAIN) (0050=500 Hz to 0200=2000 Hz) | No | - | 7 |
| `1A 05 0032` | 0050 ~ 0200 | SET > Function > Beep Sound (SUB) (0050=500 Hz to 0200=2000 Hz) | No | - | 7 |
| `1A 05 0033` | 00 or 01 | SET > Function > Sub Band Mute (TX) > Speaker/Phones (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0034` | 00 or 01 | SET > Function > Sub Band Mute (TX) > USB (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0035` | 00 or 01 | SET > Function > Sub Band Mute (TX) > LAN (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0036` | 00 to 02 | SET > Function > RF/SQL Control (00=Auto, 01=SQL, 02=RF+SQL) | No | - | 7 |
| `1A 05 0037` | 00 or 01 | SET > Function > FM/DV Center Error function (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0038` | 00 to 05 | SET > Function > TX Delay > 144M (00=OFF, 01=10 ms, 02=15 ms, 03=20 ms, 04=25 ms, 05=30 ms) | No | - | 7 |
| `1A 05 0039` | 00 to 05 | SET > Function > TX Delay > 430M (00=OFF, 01=10 ms, 02=15 ms, 03=20 ms, 04=25 ms, 05=30 ms) | No | - | 7 |
| `1A 05 0040` | 00 to 05 | SET > Function > TX Delay > 1200M (00=OFF, 01=10 ms, 02=15 ms, 03=20 ms, 04=25 ms, 05=30 ms) | No | - | 7 |
| `1A 05 0041` | 00 to 05 | SET > Function > Time-Out Timer (00=OFF, 01=3 min., 02=5 min., 03=10 min., 04=20 min., 05=30 min.) | No | - | 7 |
| `1A 05 0042` | 00 or 01 | SET > Function > PTT Lock (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0043` | 00 or 01 | SET > Function > SPLIT > Quick SPLIT (00=OFF, 01=ON) (Setting the [SPLIT] key operation when it is held down for 1 second.) | Yes | `funcQuickSplit` | 7 |
| `1A 05 0044` | See p. 16 | SET > Function > SPLIT > FM SPLIT Offset | No | - | 7 |
| `1A 05 0045` | 00 or 01 | SET > Function > SPLIT > SPLIT LOCK (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0046` | 00 or 01 | SET > Function > Auto Repeater (00=OFF, 01=ON (DUP,TONE) for USA version) | No | - | 7 |
| `1A 05 0047` | 00 to 02 | SET > Function > RTTY Mark Frequency (00=1275 Hz, 01=1615 Hz, 02=2125 Hz) | No | - | 7 |
| `1A 05 0048` | 00 to 02 | SET > Function > RTTY Shift Width (00=170 Hz, 01=200 Hz, 02=425 Hz) | No | - | 7 |
| `1A 05 0049` | 00 or 01 | SET > Function > RTTY Keying Polarity (00=Normal, 01=Reverse) | No | - | 7 |
| `1A 05 0050` | 00 or 01 | SET > Function > SPEECH > SPEECH Language (00=English, 01=Japanese) | No | - | 7 |
| `1A 05 0051` | 00 or 01 | SET > Function > SPEECH > Alphabet (00=Normal, 01=Phonetic Code) | No | - | 7 |
| `1A 05 0052` | 00 or 01 | SET > Function > SPEECH > SPEECH Speed (00=Slow, 01=Fast) | No | - | 7 |
| `1A 05 0053` | 00 to 02 | SET > Function > SPEECH > RX Call Sign SPEECH (00=OFF, 01=ON (Kerchunk), 02=ON (All)) | No | - | 7 |
| `1A 05 0054` | 00 or 01 | SET > Function > SPEECH > RX>CS SPEECH (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0055` | 00 or 01 | SET > Function > SPEECH > S-Level SPEECH | No | - | 7 |
| `1A 05 0056` | 00 or 01 | SET > Function > SPEECH > MODE SPEECH (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0057` | 0000 ~ 0255 | SET > Function > SPEECH > SPEECH Level (0000=0% to 0255=100%) | No | - | 7 |
| `1A 05 0058` | 00 or 01 | SET > Function > [SPEECH/LOCK] Switch (00=SPEECH/LOCK, 01=LOCK/SPEECH) | No | - | 7 |
| `1A 05 0059` | 00 or 01 | SET > Function > Lock Function (00=MAIN DIAL, 01=PANEL) | No | - | 7 |
| `1A 05 0060` | 00 or 01 | SET > Function > Memo Pad Quantity (00=5 ch, 01=10 ch) | No | - | 7 |
| `1A 05 0061` | 00 to 02 | SET > Function > MAIN DIAL Auto TS (00=OFF, 01=Low, 02=High) | No | - | 7 |
| `1A 05 0062` | 00 or 01 | SET > Function > MIC Up/Down Speed (00=Slow, 01=Fast) | No | - | 7 |
| `1A 05 0063` | 00 or 01 | SET > Function > AFC Limit (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0064` | 00 to 02 | SET > Function > [NOTCH] Switch (SSB) (00=Auto, 01=Manual, 02=Auto/Manual) | No | - | 7 |
| `1A 05 0065` | 00 to 02 | SET > Function > [NOTCH] Switch (AM) (00=Auto, 01=Manual, 02=Auto/Manual) | No | - | 7 |
| `1A 05 0066` | 00 or 01 | SET > Function > SSB/CW Synchronous Tuning (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0067` | 00 or 01 | SET > Function > CW Normal Side (00=LSB, 01=USB) | No | - | 7 |
| `1A 05 0068` | 00 or 01 | SET > Function > Screen Keyboard Type (00=Ten-key, 01=Full Keyboard) | No | - | 7 |
| `1A 05 0069` | 00 to 02 | SET > Function > Screen Full Keyboard Layout (00=English, 01=German, 02=French) | No | - | 7 |
| `1A 05 0070` | 00 or 01 | SET > Function > Screen Capture [POWER] Switch (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0071` | 00 or 01 | SET > Function > Screen Capture File Type (00=PNG, 01=BMP) | No | - | 7 |
| `1A 05 0072` | 0000 ~ 0255 | SET > Function > REF Adjust (0000=0%, 0255=100%) | Yes | `funcREFAdjust` | 7 |
| `1A 05 0073` | 0000 ~ 0255 | SET > Function > REF Adjust (FINE) (0000=0%, 0255=100%) | Yes | `funcREFAdjustFine` | 7 |
| `1A 05 0074` | 00 to 03 | SET > DV/DD Set > Standby Beep (00=OFF, 01=ON, 02=ON (to me: High Tone), 03=ON (to me: Alarm/High Tone)) | No | - | 7 |
| `1A 05 0075` | 00 to 02 | SET > DV/DD Set > Auto Reply (00=OFF, 01=ON, 02=Voice) | No | - | 7 |
| `1A 05 0076` | 00 or 01 | SET > DV/DD Set > DV Data TX (00=PTT 01=Auto) | No | - | 7 |
| `1A 05 0077` | 00 or 01 | SET > DV/DD Set > DV Fast Data > Fast Data (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0078` | 00 or 01 | SET > DV/DD Set > DV Fast Data > GPS Data Speed (00=Slow, 01=Fast) | No | - | 7 |
| `1A 05 0079` | 00 to 10 | SET > DV/DD Set > DV Fast Data > TX Delay (PTT) (00=OFF, 01=1 sec. to 10=10 sec.) | No | - | 7 |
| `1A 05 0080` | 00 to 02 | SET > DV/DD Set > Digital Monitor (00=Auto, 01=Digital, 02=Analog) | No | - | 7 |
| `1A 05 0081` | 00 or 01 | SET > DV/DD Set > Digital Repeater Set (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0082` | 00 or 01 | SET > DV/DD Set > DV Auto Detect (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0083` | 00 or 01 | SET > DV/DD Set > RX Record (RPT) (00=ALL, 01=Latest Only) | No | - | 7 |
| `1A 05 0084` | 00 or 01 | SET > DV/DD Set > BK (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0085` | 00 or 01 | SET > DV/DD Set > EMR (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0086` | 0000 ~ 0255 | SET > DV/DD Set > EMR AF Level (0000=0%, 0255=100%) | No | - | 7 |
| `1A 05 0087` | 00 or 01 | SET > DV/DD Set > | No | - | 7 |
| `1A 05 0088` | 00 or 01 | SET > DV/DD Set > DD Packet Output (00=Normal, 01=All) | No | - | 7 |
| `1A 05 0089` | 00 or 01 | SET > QSO/RX Log > QSO Log (00=OFF, 01=ON) | No | - | 7 |
| `1A 05 0090` | 00 or 01 | SET > QSO/RX Log > RX History Log (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0091` | 00 to 02 | SET > QSO/RX Log > CSV Format > Separator/Decimal (00=Separator is ", " and Decimal is ".," 01=Separator is "; " and Decimal is ".," 02=Separator is "; " and Decimal is ", ") | No | - | 8 |
| `1A 05 0092` | 00 to 02 | SET > QSO/RX Log > CSV Format > Date (00="yyyy/mm/dd," 01="mm/dd/yyyy," 02="dd/mm/yyyy") | No | - | 8 |
| `1A 05 0093` | 00 or 01 | SET > Connectors > External P.AMP > 144M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0094` | 00 or 01 | SET > Connectors > External P.AMP > 430M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0095` | 00 or 01 | SET > Connectors > External P.AMP > 1200M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0096` | 00 or 01 | SET > Connectors > External Speaker Separate (00=Separate, 01=Mix) | No | - | 8 |
| `1A 05 0097` | - | to 30 SET > Connectors > Phones > Level (00=-15 dB to 30=+15 dB) | No | - | 8 |
| `1A 05 0098` | 00 to 02 | SET > Connectors > Phones > L/R Mix (00=Separate, 01=Mix, 02=Auto) | No | - | 8 |
| `1A 05 0099` | 00 or 01 | SET > Connectors > ACC AF/IF Output > AF/SQL Output Select (00=MAIN, 01=SUB) | No | - | 8 |
| `1A 05 0100` | 00 or 01 | SET > Connectors > ACC AF/IF Output > Output Select (00=AF, 01=IF) | No | - | 8 |
| `1A 05 0101` | 0000 ~ 0255 | SET > Connectors > ACC AF/IF Output > | No | - | 8 |
| `1A 05 0102` | 00 or 01 | SET > Connectors > ACC AF/IF Output > | No | - | 8 |
| `1A 05 0103` | 00 or 01 | SET > Connectors > ACC AF/IF Output > | No | - | 8 |
| `1A 05 0104` | 0000 ~ 0255 | SET > Connectors > ACC AF/IF Output > ACC IF Output Level (0000=0% to 0255=100%) | No | - | 8 |
| `1A 05 0105` | 00 or 01 | SET > Connectors > USB AF/IF Output > Output Select (00=AF, 01=IF) | No | - | 8 |
| `1A 05 0106` | 0000 ~ 0255 | SET > Connectors > USB AF/IF Output > | No | - | 8 |
| `1A 05 0107` | 00 or 01 | SET > Connectors > USB AF/IF Output > | No | - | 8 |
| `1A 05 0108` | 00 or 01 | SET > Connectors > USB AF/IF Output > | No | - | 8 |
| `1A 05 0109` | 0000 ~ 0255 | SET > Connectors > USB AF/IF Output > IF Output Level (0000=0%, 0255=100%) | No | - | 8 |
| `1A 05 0110` | 00 or 01 | SET > Connectors > LAN AF/IF Output > Output Select (00=AF, 01=IF) | No | - | 8 |
| `1A 05 0111` | 00 or 01 | SET > Connectors > LAN AF/IF Output > | No | - | 8 |
| `1A 05 0112` | 0000 ~ 0255 | SET > Connectors > MOD Input > ACC MOD Level (0000=0% to 0255=100%) | Yes | `funcACCAModLevel` | 8 |
| `1A 05 0113` | 0000 ~ 0255 | SET > Connectors > MOD Input > USB MOD Level (0000=0% to 0255=100%) | Yes | `funcUSBModLevel` | 8 |
| `1A 05 0114` | 0000 ~ 0255 | SET > Connectors > MOD Input > LAN MOD Level (0000=0% to 0255=100%) | Yes | `funcLANModLevel` | 8 |
| `1A 05 0115` | 00 ~ 05 | SET > Connectors > MOD Input > DATA OFF MOD (00=MIC, 01=ACC, 02=MIC,ACC, 03=USB, 04=MIC,USB, 05=LAN) | Yes | `funcDATAOffMod` | 8 |
| `1A 05 0116` | 00 ~ 05 | SET > Connectors > MOD Input > DATA MOD | Yes | `funcDATA1Mod` | 8 |
| `1A 05 0117` | 00 or 01 | SET > Connectors > ACC SEND Output > 144M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0118` | 00 or 01 | SET > Connectors > ACC SEND Output > 430M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0119` | 00 or 01 | SET > Connectors > ACC SEND Output > 1200M (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0120` | 00 to 04 | SET > Connectors > USB SEND/Keying > USB SEND (00=OFF, 01=USB(A) DTR, 02=USB(A) RTS, 03=USB(B) DTR, 04=USB(B) RTS) (You cannot select the same setting for USB keying (CW) or USB keying (RTTY).) | No | - | 8 |
| `1A 05 0121` | 00 to 04 | SET > Connectors > USB SEND/Keying > USB Keying (CW) (00=OFF, 01=USB(A) DTR, 02=USB(A) RTS, 03=USB(B) DTR, 04=USB(B) RTS) (You cannot select the same setting for USB SEND.) | No | - | 8 |
| `1A 05 0122` | 00 to 04 | SET > Connectors > USB SEND/Keying > USB Keying (RTTY) (00=OFF, 01=USB(A) DTR, 02=USB(A) RTS, 03=USB(B) DTR, 04=USB(B) RTS) (You cannot select the same setting for USB SEND.) | No | - | 8 |
| `1A 05 0123` | 00 or 01 | SET > Connectors > USB SEND/Keying > Inhibit Timer at USB connection (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0124` | 00 or 01 | SET > Connectors > External Keypad > VOICE (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0125` | 00 or 01 | SET > Connectors > External Keypad > KEYER (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0126` | 00 or 01 | SET > Connectors > External Keypad > RTTY (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0127` | 00 or 01 | SET > Connectors > CI-V > CI-V Transceive (00=OFF, 01=ON) | Yes | `funcCIVTransceive` | 8 |
| `1A 05 0128` | - | ~ 0223 SET > Connectors > CI-V > CI-V USB/ LAN->REMOTE Transceive Address (0000=00h to 0223=DFh in Hexadecimal) | No | - | 8 |
| `1A 05 0129` | 00 or 01 | SET > Connectors > CI-V > CI-V USB Port 1 (00=Link to [REMOTE], 01=Unlink to [REMOTE]) | No | - | 8 |
| `1A 05 0130` | 00 or 01 | SET > Connectors > CI-V > CI-V USB Echo Back (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0131` | 00 or 01 | SET > Connectors > CI-V > CI-V DATA Echo Back (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0132` | 00 to 02 | SET > Connectors > CI-V > USB (B)/DATA Function > USB (B) Function (00=OFF, 01=RTTY Decode, 02=DV Data) | No | - | 8 |
| `1A 05 0133` | 00 to 04 | SET > Connectors > CI-V > USB (B)/DATA Function > DATA Function (00=OFF, 01=RTTY Decode, 02=DV Data, 03= GPS/Weather, 04= CI-V) | No | - | 8 |
| `1A 05 0134` | 00 or 01 | SET > Connectors > CI-V > USB (B)/DATA Function > GPS Out (00=OFF, 01=DATA->USB (B)) | No | - | 8 |
| `1A 05 0135` | 00 or 01 | SET > Connectors > CI-V > USB (B)/DATA Function > DV Data/GPS Out Baud Rate (00=4800bps, 01=9600bps) | No | - | 8 |
| `1A 05 0136` | 00 to 03 | SET > Connectors > CI-V > USB (B)/DATA Function > RTTY Decode Baud Rate (00=4800bps, 01=9600bps, 02=19200bps, 03=38400bps) | No | - | 8 |
| `1A 05 0137` | 00 or 01 | SET > Network > DHCP (Valid after Restart) (00=OFF, 01=ON) | No | - | 8 |
| `1A 05 0138` | 0000000000 | SET > Network > IP Address (Valid after 000001 ~ Restart) 0255025502 (0000000000000001=0.0.0.1 to 0255025502 550254 550254=255.255.255.254) (Valid when the DHCP (Valid after Restart) is set to OFF.) | No | - | 8 |
| `1A 05 0139` | 0000000000 | SET > Network > DHCP (Valid after Restart) 1 000001 ~ Read the IP address set by the DHCP server 0255025502 (0000000000000001=0.0.0.1 to 0255025502 550254 550254=255.255.255.254) (When the DHCP setting (Valid after Restart) is set to OFF, the manually set IP address (static IP address) is returned.) | No | - | 9 |
| `1A 05 0140` | - | ~ 30 SET > Network > Subnet Mask (Valid after Restart) (01=128.0.0.0 (1 bit) to 30=255.255.255.252 (30 bit)) (Valid when the DHCP (Valid after Restart) setting is set to OFF.) | No | - | 9 |
| `1A 05 0141` | 0000000000 | SET > Network > 000001 ~ Default Gateway (Valid after Restart) 0255025502 (0000000000000001=0.0.0.1 to 0255025502 550254, FF 550254=255.255.255.254, FF=Blank) (Valid when the DHCP (Valid after Restart) setting is set to OFF.) | No | - | 9 |
| `1A 05 0142` | 0000000000 | SET > Network > 000001 ~ Primary DNS Server (Valid after Restart) 0255025502 (0000000000000001=0.0.0.1 to 0255025502 550254, FF 550254=255.255.255.254, FF=Blank) (Valid when the DHCP (Valid after Restart) setting is set to OFF.) | No | - | 9 |
| `1A 05 0143` | 0000000000 | SET > Network > 000001 ~ 2nd DNS Server (Valid after Restart) 0255025502 (0000000000000001=0.0.0.1 to 0255025502 550254, FF 550254=255.255.255.254, FF=Blank) (Valid when the DHCP (Valid after Restart) setting is set to OFF.) | No | - | 9 |
| `1A 05 0144` | See p. 15 | SET > Network > Network Name (Up to 15 characters) | No | - | 9 |
| `1A 05 0145` | 00 or 01 | SET > Network > Network Control (Valid after Restart) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0146` | 00 or 01 | SET > Network > Power OFF Setting (for Remote Control) (00=Shutdown only, 01=Standby/Shutdown) | No | - | 9 |
| `1A 05 0147` | 000001 | ~ SET > Network > 065535 Control Port (UDP) (Valid after Restart) (000001=1 to 065535=65535) | No | - | 9 |
| `1A 05 0148` | 000001 | ~ SET > Network > 065535 Serial Port (UDP) (Valid after Restart) (000001=1 to 065535=65535) | No | - | 9 |
| `1A 05 0149` | 000001 | ~ SET > Network > 065535 Audio Port (UDP) (Valid after Restart) (000001=1 to 065535=65535) | No | - | 9 |
| `1A 05 0150` | 00 or 01 | SET > Network > Internet Access Line (Valid after Restart) (00=FTTH (Fiber To The Home), 01=ADSL/ CATV) | No | - | 9 |
| `1A 05 0151` | See p. 15 | SET > Network > Network Radio Name (Up to 16 characters) | No | - | 9 |
| `1A 05 0152` | 0000 ~ 0255 | SET > Display > LCD Backlight (0000=0% to 0255=100%) | No | - | 9 |
| `1A 05 0153` | 00 or 01 | SET > Display > Display Type (00=A, 01=B) | No | - | 9 |
| `1A 05 0154` | 00 or 01 | SET > Display > Display Font (00=Basic, 01=Round) | No | - | 9 |
| `1A 05 0155` | 00 or 01 | SET > Display > Meter Peak Hold (Bar) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0156` | 00 or 01 | SET > Display > Memory Name (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0157` | 00 or 01 | SET > Display > MN-Q Popup (MN OFF->ON) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0158` | 00 or 01 | SET > Display > BW Popup (PBT) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0159` | 00 or 01 | SET > Display > BW Popup (FIL) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0160` | 00 to 03 | SET > Display > RX Call Sign Display (00=OFF, 01=Normal, 02=RX Hold, 03=Hold) | No | - | 9 |
| `1A 05 0161` | 00 or 01 | SET > Display > RX Position Indicator (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0162` | 00 to 02 | SET > Display > RX Position Display (00=OFF, 01=ON (Main/Sub), 02=ON (Main Only)) | No | - | 9 |
| `1A 05 0163` | 00 to 04 | SET > Display > RX Position Display Timer (00=5 sec, 01=10 sec, 02=15 sec, 03=30 sec, 04=Hold) | No | - | 9 |
| `1A 05 0164` | 00 or 01 | SET > Display > Reply Position Display (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0165` | 00 to 02 | SET > Display > TX Call Sign Display (00=OFF, 01=Your Call Sign, 02=My Call Sign) | No | - | 9 |
| `1A 05 0166` | 00 or 01 | SET > Display > Scroll Speed (00=Slow, 01=Fast) | No | - | 9 |
| `1A 05 0167` | 00 to 03 | SET > Display > Screen Saver (00=OFF, 01=15 min., 02=30 min., 03=60 min.) | No | - | 9 |
| `1A 05 0168` | 00 or 01 | SET > Display > Opening Message (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0169` | 00 or 01 | SET > Display > Power ON Check (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0170` | 00 to 02 | SET > Display > Display Unit > Latitude/Longitude (00=ddd°mm.mm', 01=ddd°mm'ss", 02=ddd.dddd°) | No | - | 9 |
| `1A 05 0171` | 00 or 01 | SET > Display > Display Unit > Altitude/Distance (00=m, 01=ft/mi) | No | - | 9 |
| `1A 05 0172` | 00 to 02 | SET > Display > Display Unit > Speed (00=km/h, 01=mph, 02=knots) | No | - | 9 |
| `1A 05 0173` | 00 or 01 | SET > Display > Display Unit > Temperature (00= degC, 01= degF) | No | - | 9 |
| `1A 05 0174` | 00 to 03 | SET > Display > Display Unit > Barometric (00=hPa, 01=mb, 02=mmHg, 03=inHg) | No | - | 9 |
| `1A 05 0175` | 00 or 01 | SET > Display > Display Unit > Rainfall (00=mm, 01=inch) | No | - | 9 |
| `1A 05 0176` | 00 to 03 | SET > Display > Display Unit > Wind Speed (00=m/s, 01=km/h, 02=mph, 03=knots) | No | - | 9 |
| `1A 05 0177` | 00 or 01 | SET > Display > Display Language (00=English, 01=Japanese) | No | - | 9 |
| `1A 05 0178` | 00 or 01 | SET > Display > System Language (00=English, 01=Japanese) | No | - | 9 |
| `1A 05 0179` | 20000101 ~ 20991231 | SET > Time Set > Date/Time > Date (20000101=2000/01/01 to 20991231=2099/12/31) | Yes | `funcDate` | 9 |
| `1A 05 0180` | 0000 ~ 2359 | SET > Time Set > Date/Time > Time (0000=00:00 to 2359=23:59) | Yes | `funcTime` | 9 |
| `1A 05 0181` | 00 or 01 | SET > Time Set > Date/Time > NTP Function (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0182` | See p. 15 | SET > Time Set > Date/Time > NTP Server Address | No | - | 9 |
| `1A 05 0183` | 00 or 01 | SET > Time Set > Date/Time > GPS Time Correct (00=OFF, 01=Auto) | No | - | 9 |
| `1A 05 0184` | See p. 16 | SET > Time Set > UTC Offset | Yes | `funcUTCOffset` | 9 |
| `1A 05 0185` | 00 to 02 | SET > SD Card > Import/Export > CSV Format > Separator/Decimal (00=Separator is ", " and Decimal is ".," 01=Separator is "; " and Decimal is ".," 02=Separator is "; " and Decimal is ", ") | No | - | 9 |
| `1A 05 0186` | 00 to 02 | SET > SD Card > Import/Export > CSV Format > Date (00=""yyyy/mm/dd," 01="mm/dd/yyyy," 02="dd/mm/yyyy") | No | - | 9 |
| `1A 05 0187` | 00 or 01 | SCOPE > Scope during Tx (CENTER TYPE) (00=OFF, 01=ON) | No | - | 9 |
| `1A 05 0188` | 00 to 02 | SCOPE > Max Hold (00=OFF, 01=10s Hold, 02=ON) | No | - | 9 |
| `1A 05 0189` | - | ~ 02 SCOPE > CENTER Type Display (00=Filter center, 01=Carrier point center, 02=Carrier point center (Abs. Freq.)) | No | - | 9 |
| `1A 05 0190` | 00 or 01 | SCOPE > Marker Position (Fix Type/SCROLL Type) (00=Filter center, 01 Carrier point) | No | - | 9 |
| `1A 05 0191` | See p. 16 | SCOPE > VBW | No | - | 9 |
| `1A 05 0192` | 00 to 03 | SCOPE > Averaging (00=OFF, 01=2, 02=3, 03=4) | No | - | 9 |
| `1A 05 0193` | 00 or 01 | SCOPE > Waveform Type (00=Fill, 01=Fill+Line) | No | - | 9 |
| `1A 05 0194` | See p. 16 | SCOPE > Waveform Color (Current) | No | - | 9 |
| `1A 05 0195` | See p. 16 | SCOPE > Waveform Color (Line) | No | - | 9 |
| `1A 05 0196` | See p. 16 | SCOPE > Waveform Color (Max Hold) | No | - | 9 |
| `1A 05 0197` | 00 or 01 | SCOPE > Waterfall Display (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0198` | 00 to 02 | SCOPE > Waterfall Speed (00=Slow, 01=Mid, 02=Fast) | No | - | 10 |
| `1A 05 0199` | 00 to 02 | SCOPE > Waterfall Size (Expand Screen) (00=Small, 01=Mid, 02=Large) | No | - | 10 |
| `1A 05 0200` | 00 to 07 | SCOPE > Waterfall Peak Color Level (00=Grid 1 to 07=Grid 8) | No | - | 10 |
| `1A 05 0201` | 00 or 01 | SCOPE > Waterfall Marker Auto-hide (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0202` | See p. 17 | SCOPE > Fixed Edges > 144M > No.1: | No | - | 10 |
| `1A 05 0203` | See p. 17 | SCOPE > Fixed Edges > 144M > No.2: | No | - | 10 |
| `1A 05 0204` | See p. 17 | SCOPE > Fixed Edges > 144M > No.3: | No | - | 10 |
| `1A 05 0205` | See p. 17 | SCOPE > Fixed Edges > 430M > No.1: | No | - | 10 |
| `1A 05 0206` | See p. 17 | SCOPE > Fixed Edges > 430M > No.2: | No | - | 10 |
| `1A 05 0207` | See p. 17 | SCOPE > Fixed Edges > 430M > No.3: | No | - | 10 |
| `1A 05 0208` | See p. 17 | SCOPE > Fixed Edges > 1200M > No.1: | No | - | 10 |
| `1A 05 0209` | See p. 17 | SCOPE > Fixed Edges > 1200M > No.2: | No | - | 10 |
| `1A 05 0210` | See p. 17 | SCOPE > Fixed Edges > 1200M > No.3: | No | - | 10 |
| `1A 05 0211` | 00 or 01 | AUDIO SCOPE SET > FFT Scope Waveform Type (00=Line, 01=Fill) | No | - | 10 |
| `1A 05 0212` | See p. 16 | AUDIO SCOPE SET > FFT Scope Waveform Color | No | - | 10 |
| `1A 05 0213` | 00 or 01 | AUDIO SCOPE SET > FFT Scope Waterfall Display (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0214` | See p. 16 | AUDIO SCOPE SET > Oscilloscope Waveform Color | No | - | 10 |
| `1A 05 0215` | 0000 ~ 0255 | VOICE TX > TX LEVEL (0000=0%, 0255=100%) | No | - | 10 |
| `1A 05 0216` | 00 or 01 | VOICE TX SET > Auto Monitor (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0217` | - | to 15 VOICE TX SET > Repeat Time (01=1 sec. to 15=15 sec.) | No | - | 10 |
| `1A 05 0218` | 00 to 04 | KEYER 001 > Number Style (00=Normal, 01=190->ANO, 02=190->ANT, 03=90->NO, 04=90->NT) | No | - | 10 |
| `1A 05 0219` | 00 to 08 | KEYER 001 > Count Up Trigger (01=M1 to 08=M8) | No | - | 10 |
| `1A 05 0220` | - | to 9999 KEYER 001 > Present Number (0001=1 to 9999=9999) | No | - | 10 |
| `1A 05 0221` | 0000 ~ 0255 | CW-KEY SET > Side Tone Level (0000=0% to 0255=100%) | No | - | 10 |
| `1A 05 0222` | 00 or 01 | CW-KEY SET > Side Tone Level Limit (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0223` | - | to 60 CW-KEY SET > Keyer Repeat time (01=1 sec. to 60=60 sec.) | No | - | 10 |
| `1A 05 0224` | - | to 45 CW-KEY SET > Dot/Dash Ratio (28=1:1:2.8 to 45=1:1:4.5; 0.1 steps) | Yes | `funcDashRatio` | 10 |
| `1A 05 0225` | 00 to 03 | CW-KEY SET > Rise Time (00=2 msec., 01=4 msec., 02=6 msec., 03=8 msec.) | No | - | 10 |
| `1A 05 0226` | 00 or 01 | CW-KEY SET > Paddle Polarity (00=Normal, 01=Reverse) | No | - | 10 |
| `1A 05 0227` | 00 to 02 | CW-KEY SET > Key Type (00=Straight, 01=Bug, 02=Paddle) | No | - | 10 |
| `1A 05 0228` | 00 or 01 | CW-KEY SET > MIC Up/Down Keyer (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0229` | 00 to 03 | RTTY DECODE SET > FFT Scope Averaging (00=OFF, 01=2, 02=3, 03=4) | No | - | 10 |
| `1A 05 0230` | See p. 16 | RTTY DECODE SET > FFT Scope Waveform Color | No | - | 10 |
| `1A 05 0231` | 00 or 01 | RTTY DECODE SET > Decode USOS (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0232` | 00 or 01 | RTTY DECODE SET > Decode New Line Code (00=CR, LF, CR+LF, 01=CR+LF) | No | - | 10 |
| `1A 05 0233` | 00 or 01 | RTTY DECODE SET > TX USOS (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0234` | 00 or 01 | RTTY DECODE SET > Displayed Characters during Tx (Satellite) (00=Displayed Characters during RX, 01=Displayed Characters during TX) | No | - | 10 |
| `1A 05 0235` | See p. 16 | RTTY DECODE SET > Font Color (Receive) | No | - | 10 |
| `1A 05 0236` | See p. 16 | RTTY DECODE SET > Font Color (Transmit) | No | - | 10 |
| `1A 05 0237` | 00 or 01 | RTTY DECODE LOG > Decode Log | No | - | 10 |
| `1A 05 0238` | 00 or 01 | RTTY DECODE LOG > Log Set > File Type (00=Text, 01=HTML) | No | - | 10 |
| `1A 05 0239` | 00 or 01 | RTTY DECODE SET > Log Set > Time Stamp (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0240` | 00 or 01 | RTTY DECODE SET > Log Set > Time Stamp (Time) (00=Local, 01=UTC) | No | - | 10 |
| `1A 05 0241` | 00 or 01 | RTTY DECODE SET > Log Set > Time Stamp (Frequency) (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0242` | 00 or 01 | QSO RECORDER > Recorder Set > TX REC Audio (00=Direct, 01=Monitor) | No | - | 10 |
| `1A 05 0243` | 00 or 01 | QSO RECORDER > Recorder Set > RX REC Condition (00=Always, 01=Squelch Auto) | No | - | 10 |
| `1A 05 0244` | 00 or 01 | QSO RECORDER > Recorder Set > File Split (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0245` | 00 or 01 | QSO RECORDER > Recorder Set > REC Operation (00=MAIN/SUB Separate, 01=MAIN/SUB Link) | No | - | 10 |
| `1A 05 0246` | 00 or 01 | QSO RECORDER > Recorder Set > PTT Auto REC (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0247` | 00 to 03 | QSO RECORDER > Recorder Set > PRE-REC for PTT Auto REC (00=OFF, 01=5 sec., 02=10 sec., 03=15 sec.) | No | - | 10 |
| `1A 05 0248` | 00 to 03 | QSO RECORDER > Player Set > Skip Time (00=3 sec., 01=5 sec., 02=10 sec., 03=30 sec.) | No | - | 10 |
| `1A 05 0249` | 00 or 01 | SCAN SET > SCAN Speed (00=Slow, 01=Fast) | No | - | 10 |
| `1A 05 0250` | 00 or 01 | SCAN SET > SCAN Resume (00=OFF, 01=ON) | No | - | 10 |
| `1A 05 0251` | 00 to 10 | SCAN SET > Pause Timer (00=2 sec. to 09=20 sec.; 2 sec. steps, 10=HOLD) | No | - | 10 |
| `1A 05 0252` | 00 to 06 | SCAN SET > Resume Timer (00=0 sec. to 05=5 sec., 06=HOLD) | No | - | 10 |
| `1A 05 0253` | 00 to 04 | SCAN SET > Temporary Skip Timer (00=5 min., 01=10 min., 02=15 min., 03=While Scanning, 04=While Powered ON) | No | - | 10 |
| `1A 05 0254` | 00 or 01 | SCAN SET > MAIN DIAL Operation (SCAN) (00=OFF, 01=Up/Down) | No | - | 10 |
| `1A 05 0255` | 00 to 02 | GPS > GPS Set > GPS Select (00=OFF, 01=External GPS, 02=Manual) | No | - | 10 |
| `1A 05 0256` | 00 or 01 | GPS > GPS Set > GPS Receiver Baud Rate (00=4800bps, 01=9600bps) | No | - | 10 |
| `1A 05 0257` | See p. 17 | GPS > GPS Set > Manual Position | No | - | 10 |
| `1A 05 0258` | 00 to 02 | GPS > GPS TX Mode (00=OFF, 01=D-PRS, 02=NMEA) | No | - | 10 |
| `1A 05 0259` | See p. 17 | GPS > GPS TX Mode > D-PRS > Unproto Address (Up to 56 characters) | No | - | 10 |
| `1A 05 0260` | 00 to 03 | GPS > GPS TX Mode > D-PRS > TX Format (00=Position, 01=Object, 02=Item, 03=Weather) | No | - | 10 |
| `1A 05 0261` | 00 to 04 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Symbol (00=No.1, 01=No.2, 02=No.3, 03=No.4) | No | - | 10 |
| `1A 05 0262` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > the GPS-A Symbol No.1 setting (2 characters) | No | - | 10 |
| `1A 05 0263` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > the GPS-A Symbol No.2 setting (2 characters) | No | - | 10 |
| `1A 05 0264` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > the GPS-A Symbol No.3 setting (2 characters) | No | - | 10 |
| `1A 05 0265` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > the GPS-A Symbol No.4 setting (2 characters) | No | - | 10 |
| `1A 05 0266` | - | to 42 GPS > GPS TX Mode > D-PRS > TX Format > Position > SSID (00=---, 01=(-0), 02=-1 to 16=-15, 17=-A to 42=-Z) | No | - | 10 |
| `1A 05 0267` | 00 to 03 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Comment (00=1 to 03=4) | No | - | 11 |
| `1A 05 0268` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Comment 1 (Up to 43 characters) | No | - | 11 |
| `1A 05 0269` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Comment 2 (Up to 43 characters) | No | - | 11 |
| `1A 05 0270` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Comment 3 (Up to 43 characters) | No | - | 11 |
| `1A 05 0271` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Comment 4 (Up to 43 characters) | No | - | 11 |
| `1A 05 0272` | 00 to 02 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Time Stamp (00=OFF, 01=DHM, 02=HMS) | No | - | 11 |
| `1A 05 0273` | 00 or 01 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Altitude (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0274` | 00 to 02 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Data Extension (00=OFF, 01=Course/Speed, 02=Power/Height/Gain/Directivity) | No | - | 11 |
| `1A 05 0275` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Power (00=0W, 01=1W, 02=4W, 03=9W, 04=16W, 05=25W, 06=36W, 07=49W, 08=64W, 09=81W) | No | - | 11 |
| `1A 05 0276` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Height (00=3 m (10 ft), 01=6 m (20 ft), 02=12 m (40 ft), 03=24 m (80 ft), 04=49 m (160 ft), 05=98 m (320 ft), 06=195 m (640 ft), 07=390 m (1280 ft), 08=780 m (2560 ft), 09=1561 m (5120 ft)) | No | - | 11 |
| `1A 05 0277` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Gain (00=0 dB to 09=9 dB) | No | - | 11 |
| `1A 05 0278` | 00 to 08 | GPS > GPS TX Mode > D-PRS > TX Format > Position > Directivity (00=Omni, 01=45 degNE, 02=90 degE, 03=135 degSE, 04=180 degS, 05=225 degSW, 06=270 degW, 07=315 degNW, 08=360 degN) | No | - | 11 |
| `1A 05 0279` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Object Name (Up to 9 characters) | No | - | 11 |
| `1A 05 0280` | 00 or 01 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Data Type (00=Live Object, 01=Kill Object) | No | - | 11 |
| `1A 05 0281` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Symbol (2 characters) | No | - | 11 |
| `1A 05 0282` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Comment (Up to 43 characters) | No | - | 11 |
| `1A 05 0283` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Position | No | - | 11 |
| `1A 05 0284` | 00 to 02 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Data Extension (00=OFF, 01=Course/Speed, 02=Power/Height/Gain/Directivity) | No | - | 11 |
| `1A 05 0285` | 000 to 360 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Course (0 deg to 360 deg; 1 degree steps) | No | - | 11 |
| `1A 05 0286` | - | to 1850 GPS > GPS TX Mode > D-PRS > TX Format > Object > Speed (0 km/h to 1850 km/h) | No | - | 11 |
| `1A 05 0287` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Power (00=0W, 01=1W, 02=4W, 03=9W, 04=16W, 05=25W, 06=36W, 07=49W, 08=64W, 09=81W) | No | - | 11 |
| `1A 05 0288` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Height (00=3 m (10 ft), 01=6 m (20 ft), 02=12 m (40 ft), 03=24 m (80 ft), 04=49 m (160 ft), 05=98 m (320 ft), 06=195 m (640 ft), 07=390 m (1280 ft), 08=780 m (2560 ft), 09=1561 m (5120 ft)) | No | - | 11 |
| `1A 05 0289` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Gain (00=0 dB to 09=9 dB) | No | - | 11 |
| `1A 05 0290` | 00 to 08 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Directivity (00=Omni, 01=45 degNE, 02=90 degE, 03=135 degSE, 04=180 degS, 05=225 degSW, 06=270 degW, | No | - | 11 |
| `1A 05 0291` | 00 to 42 | GPS > GPS TX Mode > D-PRS > TX Format > Object > SSID (00=---, 01=(-0), 02=-1 to 16=-15, 17=-A to 42=-Z) | No | - | 11 |
| `1A 05 0292` | 00 or 01 | GPS > GPS TX Mode > D-PRS > TX Format > Object > Time Stamp (00=DHM, 01=HMS) | No | - | 11 |
| `1A 05 0293` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Item Name (Up to 9 characters) | No | - | 11 |
| `1A 05 0294` | 00 or 01 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Data Type (00=Live Item, 01=Killed Item) | No | - | 11 |
| `1A 05 0295` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Symbol (2 characters) | No | - | 11 |
| `1A 05 0296` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Comment (Up to 43 characters) | No | - | 11 |
| `1A 05 0297` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Position | No | - | 11 |
| `1A 05 0298` | 00 to 02 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Data Extension (00=OFF, 01=Course/Speed, 02=Power/Height/Gain/Directivity) | No | - | 11 |
| `1A 05 0299` | 000 to 360 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Course (0 deg to 360 deg; 1 degree steps) | No | - | 11 |
| `1A 05 0300` | - | to 1850 GPS > GPS TX Mode > D-PRS > TX Format > Item > Speed (0 km/h to 1850 km/h) | No | - | 11 |
| `1A 05 0301` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Power (00=0W, 01=1W, 02=4W, 03=9W, 04=16W, 05=25W, 06=36W, 07=49W, 08=64W, 09=81W) | No | - | 11 |
| `1A 05 0302` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Height (00=3 m (10 ft), 01=6 m (20 ft), 02=12 m (40 ft), 03=24 m (80 ft), 04=49 m (160 ft), 05=98 m (320 ft), 06=195 m (640 ft), 07=390 m (1280 ft), 08=780 m (2560 ft), 09=1561 m (5120 ft)) | No | - | 11 |
| `1A 05 0303` | 00 to 09 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Gain (00=0 dB to 09=9 dB) | No | - | 11 |
| `1A 05 0304` | 00 to 08 | GPS > GPS TX Mode > D-PRS > TX Format > Item > Directivity (00=Omni, 01=45 degNE, 02=90 degE, 03=135 degSE, 04=180 degS, 05=225 degSW, 06=270 degW, 07=315 degNW, 08=360 degN) | No | - | 11 |
| `1A 05 0305` | - | to 42 GPS > GPS TX Mode > D-PRS > TX Format > Item > SSID (00=---, 01=(-0), 02=-1 to 16=-15, 17=-A to 42=-Z) | No | - | 11 |
| `1A 05 0306` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Weather > Symbol (2 characters) | No | - | 11 |
| `1A 05 0307` | - | to 42 GPS > GPS TX Mode > D-PRS > TX Format > Weather > SSID (00=---, 01=(-0), 02=-1 to 16=-15, 17=-A to 42=-Z) | No | - | 11 |
| `1A 05 0308` | See p. 17 | GPS > GPS TX Mode > D-PRS > TX Format > Weather > Comment (Up to 43 characters) | No | - | 11 |
| `1A 05 0309` | 00 to 02 | GPS > GPS TX Mode > D-PRS > TX Format > Weather > Time Stamp (00=OFF, 01=DHM, 02=HMS) | No | - | 11 |
| `1A 05 0310` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (RMC) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0311` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (CGA) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0312` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (GLL) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0313` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (GSA) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0314` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (VTG) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0315` | 00 or 01 | GPS > GPS TX Mode > NMEA > 6 GPS Sentence (GSV) (00=OFF, 01=ON) | No | - | 11 |
| `1A 05 0316` | See p. 17 | GPS > GPS TX Mode > NMEA > GPS Message (Up to 20 characters) | No | - | 12 |
| `1A 05 0317` | See p. 17 | GPS > GPS Alarm> Alarm Area (Group) | No | - | 12 |
| `1A 05 0318` | 00 to 03 | GPS > GPS Alarm> Alarm Area (RX/Memory) (00=Limited, 01=Extended, 02=Both) | No | - | 12 |
| `1A 05 0319` | 00 to 08 | GPS > GPS Auto TX (00=OFF, 01**=5 sec., 02=10 sec., 03=30 sec., 04=1 min., 05=3 min., 06=5 min., 07=10 min., 08=30 min.) * When 4 kinds of GPS sentences are selected, you cannot select "01." | No | - | 12 |
| `1A 05 0320` | 00 to 03 | DTMF SET > DTMF Speed (00=100ms, 01=200ms, 02=300ms, 03=500 ms) | No | - | 12 |
| `1A 05 0321` | - | to 0255 Set the NB LEVEL (144 MHz) (0000=0% to 0255=100%) | No | - | 12 |
| `1A 05 0322` | 00 to 09 | Set the NB DEPTH (144 MHz) (00=1 to 09=10) | No | - | 12 |
| `1A 05 0323` | - | to 0255 Set the NB WIDTH (144 MHz) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0324` | - | to 0255 Set the NB LEVEL (430 MHz) (0000=0% to 0255=100%) | No | - | 12 |
| `1A 05 0325` | 00 to 09 | Set the NB DEPTH (430 MHz) (00=1 to 09=10) | No | - | 12 |
| `1A 05 0326` | - | to 0255 Set the NB WIDTH (430 MHz) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0327` | - | to 0255 Set the NB LEVEL (1200 MHz) (0000=0% to 0255=100%) | No | - | 12 |
| `1A 05 0328` | 00 to 09 | Set the NB DEPTH (1200 MHz) (00=1 to 09=10) | No | - | 12 |
| `1A 05 0329` | - | to 0255 Set the NB WIDTH (1200 MHz) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0330` | - | to 20 Set the VOX DELAY (00=0.0 sec. to 20=2.0 sec.; 0.1 sec steps) | No | - | 12 |
| `1A 05 0331` | 00 to 03 | Set the VOX voice delay (00=OFF, 01=Short, 02=Mid, 03=Long) | No | - | 12 |
| `1A 05 0332` | 00 or 01 | Set the TX PWR LIMIT (144M) function (00=OFF, 01=ON) | No | - | 12 |
| `1A 05 0333` | - | to 0255 Set the TX PWR LIMIT (144M) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0334` | 00 or 01 | Set the TX PWR LIMIT (430M) function (00=OFF, 01=ON) | No | - | 12 |
| `1A 05 0335` | - | to 0255 Set the TX PWR LIMIT (430M) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0336` | 00 or 01 | Set the TX PWR LIMIT (1200M) function (00=OFF, 01=ON) | No | - | 12 |
| `1A 05 0337` | - | to 0255 Set the TX PWR LIMIT (1200M) (0000=1 to 0255=100) | No | - | 12 |
| `1A 05 0338` | 00 or 01 | Set the Received Call sign Display ("Name" or "Call Sign") (00=Call Sign, 01=Name) | No | - | 12 |
| `1A 05 0339` | 00 to 02 | Set the Compass Direction (00=Heading Up, 01=North Up, 02=South Up) | No | - | 12 |
| `1A 05 0340` | 00 or 01 | SET > Function > Home CH Beep (00=OFF, 01=ON) | No | - | 12 |
| `1A 05 0341` | 00 or 01 | SET > Connectors > PTT Port Function (00=PTT Input, 01=PTT Input + SEND Output) | No | - | 12 |
| `1A 05 0342` | 00 or 01 | SET > Display > RX Picture Indicator (00=OFF, 01=ON) | No | - | 12 |
| `1A 05 0343` | See p. 17 | SET > Function > Front Key Customize > [VOX/BK-IN] | No | - | 12 |
| `1A 05 0344` | See p. 18 | SET > Function > Front Key Customize > [AUTOTUNE/AFC] | No | - | 12 |
| `1A 05 0345` | See p. 18 | SET > Function > Front Key Customize > [TONE/RX>CS] | No | - | 12 |
| `1A 05 0346` | See p. 18 | SET > Function > MIC Key Customize > [UP] | No | - | 12 |
| `1A 05 0347` | See p. 18 | SET > Function > MIC Key Customize > [DN] | No | - | 12 |
| `1A 05 0348` | See p. 17 | SCOPE > Fixed Edges > 144M > No.4 | No | - | 12 |
| `1A 05 0349` | See p. 17 | SCOPE > Fixed Edges > 430M > No.4 | No | - | 12 |
| `1A 05 0350` | See p. 17 | SCOPE > Fixed Edges > 1200M > No.4 | No | - | 12 |

## Implementation Summary

- Function command families listed: 59.
- Radio-setting rows extracted: 350.
- Function command families with at least one matching compiled command: 42.
- Radio-setting rows marked implemented: 13.

## Follow-Up Notes

- Treat unimplemented `1A 05` settings as requiring typed encoder/decoder work before adding them to `kRadioCommands`; many use strings, color values, GPS/D-PRS records, fixed-edge frequency pairs, or restart-sensitive network values.
- Several implemented prefixes represent broad command families. Before marking a UI feature complete, confirm the exact payload shape in `Commander::receiveCommand()` and with radio logs or packet captures.
