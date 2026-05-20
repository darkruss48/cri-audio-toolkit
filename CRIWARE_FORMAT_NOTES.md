# CRIWARE Audio Format - Technical Notes

## ADX Format

### Header Structure (Big Endian)

```
Offset  Size  Description
------  ----  -----------
0x00    2     Signature: 0x80 0x00
0x02    2     Data offset - 4 (actual data starts at this value + 4)
0x04    1     Encoding type (3 = ADX standard)
0x05    1     Block/Frame size (typically 18 bytes)
0x06    1     Bit depth (4 = 4-bit ADPCM)
0x07    1     Channel count (1 = mono, 2 = stereo)
0x08    4     Sample rate (Hz)
0x0C    4     Total sample count
0x10    2     Highpass frequency (Hz)
0x12    1     ADX version (3 or 4)
0x13    1     Encryption flag (0=none, 8=type8, 9=type9)
```

### Version 4 Extended Header

For ADX version 4 with encryption type 9 (Dragon Ball Dokkan Battle):

```
0x14    4     Padding/flags (usually 0)
0x18    N     History samples (N = 4*channels if channels>1, else 8)
0x18+N  24    Loop block
```

### Loop Block Structure (24 bytes, Big Endian)

```
Offset  Size  Description
------  ----  -----------
+0x00   2     Alignment/padding
+0x02   2     Loop type marker (1 = loop info present)
+0x04   4     Loop enabled flag (1 = enabled)
+0x08   4     Loop start sample
+0x0C   4     Loop start byte offset (relative to data start)
+0x10   4     Loop end sample
+0x14   4     Loop end byte offset (relative to data start)
```

**Important**: The loop marker at +0x02 must be `1` for the loop to be active.
The loop enabled flag at +0x04 must also be non-zero.

### Encryption (Type 9 - Dokkan Battle)

Key parameters:
- Start: 0x0003
- Mult:  0x0d19
- Add:   0x043b
- Mask:  0x1fff (for type 9)

XOR decryption for each frame's scale value:
```c
xor = start;
for each frame:
    scale = (encrypted_scale ^ xor) & mask;
    xor = (xor * mult + add) & 0x7fff;
```

## AFS2/AWB Container Format

### Header Structure (Little Endian)

```
Offset  Size  Description
------  ----  -----------
0x00    4     Magic: "AFS2" (0x41 0x46 0x53 0x32)
0x04    1     Version
0x05    1     Offset size (typically 4)
0x06    1     ID size (typically 2)
0x07    1     Reserved
0x08    4     Entry count
0x0C    4     Alignment (typically 32)
0x10    2*N   Entry IDs (N = entry count)
0x10+2N 4*(N+1) File offsets (offset[i+1] - offset[i] = size of file i)
```

### Version Flags (at 0x05)
- Value 4: 4-byte offsets
- Value 2: 2-byte offsets

## ACB Container Format (@UTF Tables)

ACB files use the @UTF table format from CRI Middleware. They can contain:
- **Embedded audio**: AWB data stored within the ACB itself
- **StreamAwbAfs2Header**: A 32-byte mini-header referencing an external AWB file
- **Metadata**: Track names, cue info, loop points, sequences

### @UTF Table Structure

```
Offset  Size  Description
------  ----  -----------
0x00    4     Magic: "@UTF" (0x40 0x55 0x54 0x46)
0x04    4     Table size (Big Endian)
0x08    4     Unknown/flags
0x0C    2     Rows offset (relative to 0x08)
0x0E    2     Strings offset (relative to 0x08)
0x10    2     Data offset (relative to 0x08)
0x12    2     Table name offset
0x14    2     Column count
0x16    2     Row stride
0x18    4     Row count
0x1C    ...   Column definitions
```

### Column Definition (5+ bytes each)

```
Offset  Size  Description
------  ----  -----------
0x00    1     Flags (storage in upper nibble, type in lower nibble)
0x01    4     Name offset (in string table, Big Endian)
```

**Storage types (upper nibble):**
- 0x50 (5): Constant value follows definition
- 0x30 (3): Per-row value stored in row data
- 0x10 (1): Constant value stored elsewhere

**Type codes (lower nibble):**
- 0: u8, 1: s8, 2: u16, 3: s16, 4: u32, 5: s32
- 6: u64, 7: s64, 8: float, 9: double
- 10 (0xA): string (offset into string table)
- 11 (0xB): data blob (offset + size in data area)

### Important Nested Tables in ACB

| Table Name | Purpose |
|------------|---------|
| CueName | Track/cue names |
| Waveform | Format info (EncodeType, SamplingRate, NumChannels, LoopFlag) |
| WaveformExtensionData | **LoopStart**, **LoopEnd** (in samples) |
| Synth | Synthesis/playback parameters |
| Track | Track definitions |
| Sequence | Sequence timing |

### WaveformExtensionData - Loop Points

When parsing ACB, the WaveformExtensionData table contains:
- **LoopStart**: Loop start point in samples (stored as constant u32)
- **LoopEnd**: Loop end point in samples (stored as constant u32)

Example from bgm_484.acb:
```
LoopStart: 397018 samples (14.179 sec at 28000 Hz)
LoopEnd: 810555 samples (28.948 sec at 28000 Hz)
```

Note: These values may differ slightly from ADX header loop points (off by ~6 samples due to frame alignment).

## Analyzed Files

### bgm_484.acb / bgm_484.awb

- **Sample rate**: 28000 Hz
- **Channels**: 2 (stereo)
- **Total samples**: 810602
- **Duration**: ~28.95 seconds
- **Encryption**: Type 9 (Dokkan Battle)
- **Container**: ACB + external AWB

#### Loop Points Discovered:
- **Loop START**: Sample 397024 = **14.179 seconds**
- **Loop END**: Sample 810561 = **28.949 seconds**
- **Loop duration**: ~14.77 seconds

This means the track plays:
1. Full intro (0 - 14.179 sec)
2. Looping section (14.179 - 28.949 sec) repeats infinitely

## Frame/Block Calculations

- Samples per frame: 32
- Frame size: 18 bytes
- Frames per channel block: 1

For stereo:
- Bytes per stereo frame pair: 18 * 2 = 36 bytes
- Loop start byte = data_offset + (start_sample / 32) * 36
- Loop end byte = data_offset + (end_sample / 32) * 36

## Command Examples

### Show container info
```bash
CriAudioExtractor --info bgm_484.acb
```
Output:
```
=== CRIWARE Container Info ===
Type: ACB + external AWB
ACB: bgm_484.acb
AWB: bgm_484.awb
Track count: 1

=== Tracks ===
Track 0:
  Format: ADX
  Size: 915456 bytes
  Sample rate: 28000 Hz
  Channels: 2
  Total samples: 810602
  Duration: 28.950 sec
  Loop: YES
    Start: 14.179 sec (sample 397024)
    End: 28.949 sec (sample 810561)
```

### Extract with metadata JSON
```bash
CriAudioExtractor bgm_484.awb output_dir --with-metadata
```
Creates `metadata.json` with full track info for round-trip rebuilding.

### Extract with decryption and WAV conversion
```bash
CriAudioExtractor bgm_484.awb output_dir --decrypt-adx 9 --to-wav
```

### Create AWB with explicit loop points
```bash
CriAudioExtractor --make-awb output.awb input.mp3 --loop-start-sec 10.0 --loop-end-sec 60.0
```

### Create AWB with auto-detected loop points
```bash
CriAudioExtractor --make-awb output.awb input.mp3 --auto-loop
```

### Create ACB+AWB from template
```bash
CriAudioExtractor --make-acb-awb template.acb output.acb output.awb input.mp3 --loop-start-sec 14.0
```

