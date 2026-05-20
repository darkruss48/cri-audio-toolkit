# CriAudioExtractor

A cross-platform C++ command-line tool for demuxing audio tracks from CRI Middleware ACB and AWB audio archives. Supports on-the-fly ADX decryption (XOR keystream) and ADX-to-WAV conversion (ADPCM decode) without any external dependencies.
I tested it with assets coming from Dragon Ball Z : Dokkan Battle.

---

## Supported Formats

| Input       | Description                                          |
|-------------|------------------------------------------------------|
| `.awb`      | CRI Audio Wave Bank — raw AFS2 archive               |
| `.acb`      | CRI Audio Cue Board — @UTF container (embedded AWB) |

| Output      | Condition                                            |
|-------------|------------------------------------------------------|
| `.adx`      | Default for unencrypted CRI ADX streams              |
| `.hca`      | CRI HCA audio streams                               |
| `.aix`      | CRI AIX interleaved audio streams                   |
| `.wav`      | When `--to-wav` is specified (PCM 16-bit output)     |
| `.bin`      | Unrecognized stream format (raw fallback)            |

---

## Build

### Requirements

- CMake >= 3.10
- A C++17 compatible compiler (GCC, Clang, or MSVC)

### Linux / macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Windows (MSVC via Developer Command Prompt)

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The binary will be located in `build/` (Linux) or `build/Release/` (Windows).

---

## Usage

There are two main workflows:

- Extract existing banks (`.acb` / `.awb`) and optionally decrypt/decode.
- Build new CRI assets (`.adx`, `.awb`, and template-based `.acb` + `.awb`) from `.mp3` / `.wav` / `.adx`.

```
CriAudioExtractor <input_file> [output_directory] [options]
```

| Option                   | Description                                                                 |
|--------------------------|-----------------------------------------------------------------------------|
| `--decrypt-adx <key_id>` | Decrypt ADX frames using the specified XOR key (see key table below)       |
| `--to-wav`               | Decode ADX ADPCM and write output as uncompressed PCM WAV (16-bit, stereo) |
| `--encrypt-adx <in> <out> [--key <id>]` | Encrypt a standalone ADX file using a key (default: `9`) |
| `--decrypt-adx-file <id> <in> <out>` | Decrypt a standalone ADX file with key id            |
| `--adx-to-wav <in.adx> <out.wav> [--decrypt-key <id>] [--loop-seconds <sec>]` | Convert ADX to WAV, optional loop rendering up to target duration |
| `--to-criware <in.adx|in.wav|in.mp3> <out.adx> [--key <id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]` | Build ADX + optional loop points |
| `--make-awb <out.awb> <input1> [input2 ...] [--key <id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]` | Generate AFS2 AWB from ADX/WAV/MP3 |
| `--make-acb-awb <template.acb> <out.acb> <out.awb> <input1> [input2 ...] [--key <id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]` | Generate AWB and clone ACB from template |

### Examples

Extract all tracks from an AWB archive:
```bash
./CriAudioExtractor sounds.awb ./output
```

Extract, decrypt, and convert ADX tracks to WAV:
```bash
./CriAudioExtractor sounds.awb ./output --decrypt-adx 9 --to-wav
```

Output includes loop information when present:
```
Extracting  track_0.adx  (915456 bytes)
    Loop: START=14.1794s (sample 397024), END=28.9486s (sample 810561)
    ADX decrypted  [key 9: Dragon Ball Z: Dokkan Battle]
```

Extract from an ACB file with embedded audio:
```bash
./CriAudioExtractor cue_bank.acb ./output --to-wav
```

Convert MP3/WAV to CRIWARE ADX (encrypted with Dokkan key 9):
```bash
./CriAudioExtractor --to-criware music.mp3 music_dokkan.adx --key 9
```

Convert MP3 to ADX with an intro + loop section:
```bash
./CriAudioExtractor --to-criware music.mp3 music_looped.adx --key 9 --loop-start-sec 12 --loop-end-sec 40
```

Auto-detect intro/loop and avoid silent tail (no manual loop points):
```bash
./CriAudioExtractor --to-criware music.mp3 music_auto_loop.adx --key 9 --auto-loop
```

Decrypt encrypted ADX and convert to WAV for listening checks:
```bash
./CriAudioExtractor --adx-to-wav music_dokkan.adx music_check.wav --decrypt-key 9
```

Render a loop preview WAV with exact duration (intro once, then loop segment repeated):
```bash
./CriAudioExtractor --adx-to-wav music_looped.adx music_preview_90s.wav --decrypt-key 9 --loop-seconds 90
```

Generate AWB directly from MP3/WAV/ADX:
```bash
./CriAudioExtractor --make-awb custom.awb music.mp3 --key 9
```

Generate AWB with loop points for each converted ADX track:
```bash
./CriAudioExtractor --make-awb custom_loop.awb music.mp3 --key 9 --loop-start-sec 12 --loop-end-sec 40
```

Generate AWB with automatic loop detection:
```bash
./CriAudioExtractor --make-awb custom_auto_loop.awb music.mp3 --key 9 --auto-loop
```

Generate ACB+AWB using an existing ACB template:
```bash
./CriAudioExtractor --make-acb-awb bgm_505.acb custom.acb custom.awb music.mp3 --key 9
```

Generate template-based ACB+AWB with loop points:
```bash
./CriAudioExtractor --make-acb-awb bgm_505.acb custom_loop.acb custom_loop.awb music.mp3 --key 9 --loop-start-sec 12 --loop-end-sec 40
```

Template-based ACB+AWB with automatic loop detection:
```bash
./CriAudioExtractor --make-acb-awb bgm_505.acb custom_auto.acb custom_auto.awb music.mp3 --key 9 --auto-loop
```

Round-trip ADX encryption/decryption test with key 9:
```bash
./CriAudioExtractor --decrypt-adx-file 9 input_encrypted.adx input_plain.adx
./CriAudioExtractor --encrypt-adx input_plain.adx input_reencrypted.adx --key 9
```

### Important Note About ACB Generation

`--make-acb-awb` is template-based: it copies cue metadata from the template ACB and patches the embedded `StreamAwbAfs2Header` blob using the generated AWB header. This is practical for game modding workflows where a known-good ACB structure already exists.

---

## Looping (Important)

This project supports ADX loop metadata so you can have:

- an intro played once
- a loop segment repeated infinitely by the game engine

This is not a full restart from sample 0 each time.

### How to think about it

- `--loop-start-sec`: where the infinite loop starts (after intro).
- `--loop-end-sec`: where loop jumps back to `loop-start-sec`.
- If `--loop-end-sec` is omitted, loop end defaults to the stream end.

### Auto mode

- `--auto-loop` tries to infer loop start by matching the end section against earlier audio (intro + body pattern).
- It also trims trailing near-silence before setting loop end, to prevent "infinite loop but silent at the end" behavior.
- If detection confidence is low, it falls back to a conservative loop start around 2 seconds.

### Infinite in-game vs fixed-length preview

- In-game: ADX loop metadata is used for infinite playback.
- Preview/export: `--loop-seconds <sec>` creates a WAV of exact duration by rendering intro once then repeating only the loop section.

Practical workflow for "just make it XX seconds":
1. Build looped ADX/AWB/ACB with `--auto-loop`.
2. Preview with exact duration using `--loop-seconds <XX>`.

---

## ADX Decryption Keys

The following key IDs are supported via `--decrypt-adx <id>`:

| ID | Name                             | Type |
|----|----------------------------------|------|
| 0  | Clover Studio (GOD HAND, Okami) | 8    |
| 1  | Grasshopper Manufacture 0 (Blood+) | 8 |
| 2  | Grasshopper Manufacture 1 (Killer7) | 8 |
| 3  | Grasshopper Manufacture 2 (Samurai Champloo) | 8 |
| 4  | Moss Ltd (Raiden III)           | 8    |
| 5  | Sonic Team 0 (Phantasy Star Universe) | 8 |
| 6  | G.dev (Senko no Ronde)          | 8    |
| 7  | Sonic Team 1 (NiGHTS: Journey of Dreams) | 8 |
| 8  | Phantasy Star Online 2          | 9    |
| 9  | Dragon Ball Z: Dokkan Battle    | 9    |

ADX encryption is a per-frame XOR keystream using a linear congruential generator (`xor = (xor * mult + add) & 0x7FFF`). The encryption type (8 or 9) determines the bitmask applied (`0x7FFF` or `0x1FFF`).

---

## How It Works

### ACB/AWB Parsing

AWB files are AFS2 archives — a flat table of binary audio streams preceded by a header containing:
- Track count and offset field size
- A list of 16-bit track IDs
- A list of byte offsets to each stream (padded to the archive's alignment boundary)

ACB files are @UTF metadata tables. They may embed an AWB archive internally; the extractor locates it by scanning for the `AFS2` magic bytes.

### ADX Decryption

Encrypted ADX files carry an encryption type flag at header offset `0x13` (`0x08` or `0x09`). The extractor:
1. Reads the flag and clears it in the output.
2. Initialises the XOR state to the key's `start` value.
3. For each 18-byte ADPCM frame, XORs the 2-byte scale value with the current state.
4. Advances the state: `xor = (xor * mult + add) & 0x7FFF`.

### ADX-to-WAV Conversion

The extractor implements a full CRI ADPCM decoder:
1. Computes the high-pass filter coefficients from the cutoff frequency stored in the ADX header.
2. Iterates over 18-byte frames (2-byte scale + 32 4-bit nibbles per channel).
3. Reconstructs each PCM sample using the predictor: `sample = nibble * scale + coef1 * hist1 + coef2 * hist2`.
4. Writes a standard RIFF WAV file with PCM 16-bit signed interleaved samples.

---

## License

This project is released under the MIT License.
