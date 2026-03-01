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

```
CriAudioExtractor <input_file> [output_directory] [options]
```

| Option                   | Description                                                                 |
|--------------------------|-----------------------------------------------------------------------------|
| `--decrypt-adx <key_id>` | Decrypt ADX frames using the specified XOR key (see key table below)       |
| `--to-wav`               | Decode ADX ADPCM and write output as uncompressed PCM WAV (16-bit, stereo) |

### Examples

Extract all tracks from an AWB archive:
```bash
./CriAudioExtractor sounds.awb ./output
```

Extract, decrypt, and convert ADX tracks to WAV:
```bash
./CriAudioExtractor sounds.awb ./output --decrypt-adx 9 --to-wav
```

Extract from an ACB file with embedded audio:
```bash
./CriAudioExtractor cue_bank.acb ./output --to-wav
```

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
