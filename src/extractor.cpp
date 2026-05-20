#include "extractor.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace fs = std::filesystem;

namespace CriAudio {

    struct AdxKey {
        int start;
        int mult;
        int add;
        int type9;
        const char* name;
    };

    struct AdxLoopInfo {
        bool hasLoop = false;
        uint32_t loopStartSample = 0;
        uint32_t loopEndSample = 0;
    };

    struct DecodedAdx {
        uint32_t sampleRate = 0;
        uint32_t totalSamples = 0;
        int channels = 0;
        std::vector<int16_t> pcm;
    };

    const AdxKey KEYS[] = {
        {0x49e1, 0x4a57, 0x553d, 0, "Clover Studio"},
        {0x5f5d, 0x58bd, 0x55ed, 0, "Grasshopper Manufacture 0"},
        {0x50fb, 0x5803, 0x5701, 0, "Grasshopper Manufacture 1"},
        {0x4f3f, 0x472f, 0x562f, 0, "Grasshopper Manufacture 2"},
        {0x66f5, 0x58bd, 0x4459, 0, "Moss Ltd"},
        {0x5deb, 0x5f27, 0x673f, 0, "Sonic Team 0"},
        {0x46d3, 0x5ced, 0x474d, 0, "G.dev"},
        {0x440b, 0x6539, 0x5723, 0, "Sonic Team 1"},
        {0x07d2, 0x1ec5, 0x0c7f, 1, "Phantasy Star Online 2"},
        {0x0003, 0x0d19, 0x043b, 1, "Dragon Ball Z: Dokkan Battle"},
    };

    static bool isEncryptedAdxFlag(uint8_t flag) {
        return flag == 8 || flag == 9;
    }

    static uint16_t readBe16(const std::vector<char>& b, size_t off) {
        return (uint16_t)(((uint8_t)b[off] << 8) | (uint8_t)b[off + 1]);
    }

    static uint32_t readBe32(const std::vector<char>& b, size_t off) {
        return ((uint32_t)(uint8_t)b[off] << 24) |
               ((uint32_t)(uint8_t)b[off + 1] << 16) |
               ((uint32_t)(uint8_t)b[off + 2] << 8) |
               (uint32_t)(uint8_t)b[off + 3];
    }

    static void writeBe16(std::vector<char>& b, size_t off, uint16_t v) {
        b[off] = (char)((v >> 8) & 0xFF);
        b[off + 1] = (char)(v & 0xFF);
    }

    static void writeBe32(std::vector<char>& b, size_t off, uint32_t v) {
        b[off] = (char)((v >> 24) & 0xFF);
        b[off + 1] = (char)((v >> 16) & 0xFF);
        b[off + 2] = (char)((v >> 8) & 0xFF);
        b[off + 3] = (char)(v & 0xFF);
    }

    static bool getAdxLoopBlockOffset(const std::vector<char>& adx, size_t& loopsOffset) {
        if (adx.size() < 0x20 || (uint8_t)adx[0] != 0x80 || (uint8_t)adx[1] != 0x00) {
            return false;
        }

        uint32_t startOffset = readBe16(adx, 0x02) + 4;
        if (startOffset > adx.size()) {
            return false;
        }

        uint16_t version = readBe16(adx, 0x12);
        uint8_t channels = (uint8_t)adx[0x07];
        if (channels == 0) {
            return false;
        }

        if ((version & 0xFF00) == 0x0300) {
            size_t off = 0x14;
            if (off + 0x18 <= adx.size() && off + 0x18 <= startOffset) {
                loopsOffset = off;
                return true;
            }
            return false;
        }

        if ((version & 0xFF00) == 0x0400) {
            size_t histSize = (channels > 1) ? (size_t)0x04 * channels : 0x08;
            size_t off = 0x18 + histSize;
            if (off + 0x18 <= adx.size() && off + 0x18 <= startOffset) {
                loopsOffset = off;
                return true;
            }
            return false;
        }

        return false;
    }

    static bool ensureLoopBlockSpace(std::vector<char>& adx) {
        if (adx.size() < 0x20 || (uint8_t)adx[0] != 0x80 || (uint8_t)adx[1] != 0x00) {
            return false;
        }

        size_t loopsOffset = 0;
        if (getAdxLoopBlockOffset(adx, loopsOffset)) {
            return true;
        }

        uint16_t version = readBe16(adx, 0x12);
        uint8_t channels = (uint8_t)adx[0x07];
        uint32_t oldStartOffset = readBe16(adx, 0x02) + 4;
        if (oldStartOffset < 6 || oldStartOffset > adx.size()) {
            return false;
        }

        uint32_t requiredStartOffset = oldStartOffset;
        if ((version & 0xFF00) == 0x0300) {
            requiredStartOffset = 0x2C; // base 0x14 + loop block 0x18
        } else if ((version & 0xFF00) == 0x0400) {
            if (channels == 0) {
                return false;
            }
            uint32_t histSize = (channels > 1) ? (uint32_t)0x04 * channels : 0x08;
            requiredStartOffset = 0x18 + histSize + 0x18;
        } else {
            return false;
        }

        if (requiredStartOffset <= oldStartOffset) {
            return false;
        }

        uint32_t delta = requiredStartOffset - oldStartOffset;
        size_t insertPos = (size_t)oldStartOffset - 6; // keep "(c)CRI" at start_offset-6
        adx.insert(adx.begin() + (std::ptrdiff_t)insertPos, delta, 0);

        writeBe16(adx, 0x02, (uint16_t)(requiredStartOffset - 4));

        return getAdxLoopBlockOffset(adx, loopsOffset);
    }

    static AdxLoopInfo parseAdxLoopInfo(const std::vector<char>& adx) {
        AdxLoopInfo info;
        size_t loopsOffset = 0;
        if (!getAdxLoopBlockOffset(adx, loopsOffset)) {
            return info;
        }

        // Loop block structure (24 bytes, Big Endian):
        // +0x00 (2 bytes): alignment/padding
        // +0x02 (2 bytes): loop type marker (1 = loop info present)
        // +0x04 (4 bytes): loop enabled flag (1 = enabled)
        // +0x08 (4 bytes): loop start sample
        // +0x0C (4 bytes): loop start byte offset
        // +0x10 (4 bytes): loop end sample
        // +0x14 (4 bytes): loop end byte offset

        uint16_t loopMarker = readBe16(adx, loopsOffset + 0x02);
        uint32_t loopEnabled = readBe32(adx, loopsOffset + 0x04);
        uint32_t start = readBe32(adx, loopsOffset + 0x08);
        uint32_t end = readBe32(adx, loopsOffset + 0x10);

        // Loop is valid if marker is 1, enabled flag is set, and end > start
        if (loopMarker == 1 && loopEnabled != 0 && end > start) {
            info.hasLoop = true;
            info.loopStartSample = start;
            info.loopEndSample = end;
        }
        return info;
    }

    static bool decodeAdxPcm(const std::vector<char>& adxData, DecodedAdx& out) {
        if (adxData.size() < 24) {
            return false;
        }

        uint32_t startoff = readBe16(adxData, 0x02) + 4;
        int channels = (uint8_t)adxData[0x07];
        uint32_t sampleRate = readBe32(adxData, 0x08);
        uint32_t totalSamples = readBe32(adxData, 0x0C);
        uint16_t highpass = readBe16(adxData, 0x10);

        if (channels <= 0 || channels > 2 || sampleRate == 0 || totalSamples == 0) {
            return false;
        }

        double PI_VAL = 3.14159265358979323846;
        double a = std::sqrt(2.0) - std::cos(2.0 * PI_VAL * highpass / sampleRate);
        double b = std::sqrt(2.0) - 1.0;
        double c = (a - std::sqrt((a + b) * (a - b))) / b;
        double coef1 = c * 2.0;
        double coef2 = -(c * c);

        std::vector<int16_t> pcm((size_t)totalSamples * channels);
        int sampleIdx[2] = {0, 0};
        int hist1[2] = {0, 0};
        int hist2[2] = {0, 0};

        int inOffset = (int)startoff;
        int frameSize = 18;
        for (uint32_t i = 0; (size_t)(inOffset + frameSize) <= adxData.size(); i++) {
            int ch = i % channels;
            if ((uint32_t)sampleIdx[ch] >= totalSamples) {
                inOffset += frameSize;
                continue;
            }

            const uint8_t* frame = (const uint8_t*)&adxData[inOffset];
            inOffset += frameSize;
            int scale = (frame[0] << 8) | frame[1];

            for (int s = 0; s < 32 && (uint32_t)sampleIdx[ch] < totalSamples; s++) {
                int byteIdx = 2 + (s / 2);
                uint8_t val = frame[byteIdx];
                int nibble = (s % 2 == 0) ? (val >> 4) : (val & 0x0F);
                if (nibble & 0x08) nibble -= 16;

                double predicted = coef1 * hist1[ch] + coef2 * hist2[ch];
                double sample = (nibble * scale) + predicted;

                int32_t intSample = (int32_t)sample;
                if (intSample > 32767) intSample = 32767;
                if (intSample < -32768) intSample = -32768;

                pcm[(size_t)sampleIdx[ch] * channels + ch] = (int16_t)intSample;
                hist2[ch] = hist1[ch];
                hist1[ch] = intSample;
                sampleIdx[ch]++;
            }
        }

        out.sampleRate = sampleRate;
        out.totalSamples = totalSamples;
        out.channels = channels;
        out.pcm = std::move(pcm);
        return true;
    }

    static uint32_t estimateLoopStartSample(const DecodedAdx& dec, uint32_t loopEndSample) {
        if (dec.channels <= 0 || dec.sampleRate == 0 || dec.totalSamples == 0 || loopEndSample <= dec.sampleRate) {
            return 0;
        }

        uint32_t endSample = std::min(loopEndSample, dec.totalSamples);
        uint32_t window = std::min<uint32_t>(dec.sampleRate * 2, endSample / 3);
        if (window < dec.sampleRate / 2 || endSample <= window + dec.sampleRate) {
            return std::min<uint32_t>(dec.sampleRate * 2, endSample / 4);
        }

        uint32_t tailStart = endSample - window;
        uint32_t searchStart = dec.sampleRate / 2;
        uint32_t searchEnd = (tailStart > window) ? (tailStart - window) : searchStart;
        uint32_t step = std::max<uint32_t>(256, dec.sampleRate / 40);
        int stride = 8;

        auto monoAt = [&](uint32_t s) -> double {
            size_t idx = (size_t)s * dec.channels;
            if (dec.channels == 1) {
                return dec.pcm[idx];
            }
            return ((double)dec.pcm[idx] + (double)dec.pcm[idx + 1]) * 0.5;
        };

        double best = -1.0;
        uint32_t bestSample = 0;

        for (uint32_t cand = searchStart; cand + window < searchEnd; cand += step) {
            double dot = 0.0;
            double e1 = 0.0;
            double e2 = 0.0;

            for (uint32_t i = 0; i < window; i += stride) {
                double a = monoAt(cand + i);
                double b = monoAt(tailStart + i);
                dot += a * b;
                e1 += a * a;
                e2 += b * b;
            }

            if (e1 <= 1.0 || e2 <= 1.0) {
                continue;
            }
            double corr = dot / (std::sqrt(e1) * std::sqrt(e2));
            if (corr > best) {
                best = corr;
                bestSample = cand;
            }
        }

        if (best >= 0.65) {
            return bestSample;
        }

        return std::min<uint32_t>(dec.sampleRate * 2, endSample / 4);
    }

    static bool setAdxLoopPoints(std::vector<char>& adx, double loopStartSec, double loopEndSec, bool autoDetectIntro) {
        size_t loopsOffset = 0;
        if (!getAdxLoopBlockOffset(adx, loopsOffset)) {
            if (!ensureLoopBlockSpace(adx) || !getAdxLoopBlockOffset(adx, loopsOffset)) {
                std::cerr << "  Error: this ADX header does not expose writable loop metadata." << std::endl;
                return false;
            }
        }

        uint32_t startOffset = readBe16(adx, 0x02) + 4;
        uint8_t channels = (uint8_t)adx[0x07];
        uint32_t sampleRate = readBe32(adx, 0x08);
        uint32_t totalSamples = readBe32(adx, 0x0C);
        if (sampleRate == 0 || channels == 0 || totalSamples == 0) {
            std::cerr << "  Error: invalid ADX stream parameters for loop patching." << std::endl;
            return false;
        }

        uint32_t loopStartSample = (uint32_t)std::llround(loopStartSec * (double)sampleRate);
        uint32_t loopEndSample = (loopEndSec <= 0.0)
            ? totalSamples
            : (uint32_t)std::llround(loopEndSec * (double)sampleRate);

        DecodedAdx dec;
        bool hasDec = false;

        if (autoDetectIntro) {
            hasDec = decodeAdxPcm(adx, dec);
            if (!hasDec) {
                std::cerr << "  Warning: auto-loop decode failed, using fallback loop points." << std::endl;
            }
        }

        // Auto-trim trailing silence when loop end is not explicitly provided.
        if (autoDetectIntro && loopEndSec <= 0.0 && hasDec) {
            uint32_t effectiveEnd = dec.totalSamples;
            uint32_t win = std::max<uint32_t>(512, dec.sampleRate / 20);

            auto monoAbsAvg = [&](uint32_t start, uint32_t count) -> double {
                if (count == 0) return 0.0;
                double acc = 0.0;
                for (uint32_t i = 0; i < count; ++i) {
                    size_t idx = (size_t)(start + i) * dec.channels;
                    int32_t m = (dec.channels == 1)
                        ? dec.pcm[idx]
                        : (int32_t)(((int)dec.pcm[idx] + (int)dec.pcm[idx + 1]) / 2);
                    acc += std::abs(m);
                }
                return acc / count;
            };

            while (effectiveEnd > win * 2) {
                uint32_t start = effectiveEnd - win;
                double avg = monoAbsAvg(start, win);
                if (avg < 300.0) {
                    effectiveEnd -= win;
                } else {
                    break;
                }
            }

            if (effectiveEnd > dec.sampleRate) {
                loopEndSample = std::min(loopEndSample, effectiveEnd);
            }
        }

        // If auto-loop is enabled and start isn't manually given, estimate intro length.
        if (autoDetectIntro && loopStartSec <= 0.0 && hasDec) {
            loopStartSample = estimateLoopStartSample(dec, loopEndSample);
        }

        if (loopStartSample >= totalSamples) {
            std::cerr << "  Error: loop start is beyond stream length." << std::endl;
            return false;
        }
        if (loopEndSample == 0 || loopEndSample > totalSamples) {
            loopEndSample = totalSamples;
        }
        if (loopEndSample <= loopStartSample) {
            std::cerr << "  Error: loop end must be greater than loop start." << std::endl;
            return false;
        }

        uint32_t bytesPerBlockAllChannels = 18u * channels;
        uint32_t startBlock = loopStartSample / 32u;
        uint32_t endBlock = loopEndSample / 32u;
        uint32_t loopStartByte = startOffset + startBlock * bytesPerBlockAllChannels;
        uint32_t loopEndByte = startOffset + endBlock * bytesPerBlockAllChannels;

        writeBe16(adx, loopsOffset + 0x00, 0); // no initial padding
        writeBe16(adx, loopsOffset + 0x02, 1); // loop info marker
        writeBe32(adx, loopsOffset + 0x04, 1); // loop enabled
        writeBe32(adx, loopsOffset + 0x08, loopStartSample);
        writeBe32(adx, loopsOffset + 0x0C, loopStartByte);
        writeBe32(adx, loopsOffset + 0x10, loopEndSample);
        writeBe32(adx, loopsOffset + 0x14, loopEndByte);

        std::cout << "  Loop metadata patched: start=" << ((double)loopStartSample / (double)sampleRate)
                  << "s, end="
                  << ((double)loopEndSample / (double)sampleRate) << "s" << std::endl;
        return true;
    }

    static bool applyAdxCipher(std::vector<char>& trackBuffer, int keyId, bool encrypt, bool strictFlagCheck, bool logResult) {
        if (keyId < 0 || keyId >= 10) {
            std::cerr << "  Error: invalid ADX key ID (" << keyId << ")" << std::endl;
            return false;
        }

        if (trackBuffer.size() < 24) {
            std::cerr << "  Error: ADX buffer is too small." << std::endl;
            return false;
        }

        const AdxKey& k = KEYS[keyId];
        uint8_t& encFlag = reinterpret_cast<uint8_t&>(trackBuffer[0x13]);

        if (strictFlagCheck) {
            if (encrypt && isEncryptedAdxFlag(encFlag)) {
                std::cerr << "  Error: ADX is already encrypted." << std::endl;
                return false;
            }
            if (!encrypt && !isEncryptedAdxFlag(encFlag)) {
                std::cerr << "  Error: ADX does not appear to be encrypted." << std::endl;
                return false;
            }
        }

        int startoff = (((uint8_t)trackBuffer[2] << 8) | (uint8_t)trackBuffer[3]) + 4;
        int channels = (uint8_t)trackBuffer[7];
        uint32_t totalSamples = ((uint8_t)trackBuffer[12] << 24) | ((uint8_t)trackBuffer[13] << 16) | ((uint8_t)trackBuffer[14] << 8) | (uint8_t)trackBuffer[15];

        int blocks = (int)((totalSamples + 31) / 32);
        int endoff = blocks * 18 * channels + startoff;

        if ((size_t)endoff > trackBuffer.size()) {
            endoff = (int)trackBuffer.size();
        }

        int mask = k.type9 ? 0x1FFF : 0x7FFF;
        int xor_val = k.start;

        for (int off = startoff; off <= endoff - 18; off += 18) {
            int val = ((uint8_t)trackBuffer[off] << 8) | (uint8_t)trackBuffer[off + 1];
            val = (val ^ xor_val) & mask;
            trackBuffer[off] = (char)((val >> 8) & 0xFF);
            trackBuffer[off + 1] = (char)(val & 0xFF);

            xor_val = (xor_val * k.mult + k.add) & 0x7FFF;
        }

        // Keep degod-compatible behavior: encrypted output flag is 8.
        encFlag = encrypt ? 8 : 0;

        if (logResult) {
            std::cout << "      ADX " << (encrypt ? "encrypted" : "decrypted")
                      << "  [key " << keyId << ": " << k.name << "]" << std::endl;
        }
        return true;
    }

    static bool readBinaryFile(const std::string& path, std::vector<char>& buffer) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "  Error: could not open input file " << path << std::endl;
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0) {
            std::cerr << "  Error: input file is empty: " << path << std::endl;
            return false;
        }

        buffer.resize((size_t)size);
        if (!file.read(buffer.data(), size)) {
            std::cerr << "  Error: failed to read " << path << std::endl;
            return false;
        }
        return true;
    }

    static bool writeBinaryFile(const std::string& path, const std::vector<char>& buffer) {
        std::ofstream outfile(path, std::ios::binary);
        if (!outfile) {
            std::cerr << "  Error: could not open output file " << path << std::endl;
            return false;
        }
        outfile.write(buffer.data(), (std::streamsize)buffer.size());
        return true;
    }

    static bool isAdxFileBuffer(const std::vector<char>& buffer) {
        return buffer.size() >= 2 && (uint8_t)buffer[0] == 0x80 && (uint8_t)buffer[1] == 0x00;
    }

    static bool isFfmpegAvailable() {
#ifdef _WIN32
        int ret = std::system("ffmpeg -version >nul 2>&1");
#else
        int ret = std::system("ffmpeg -version >/dev/null 2>&1");
#endif
        return ret == 0;
    }

    static bool encodeWithFfmpegToAdx(const std::string& inputPath, const std::string& outputPath) {
        if (!isFfmpegAvailable()) {
            std::cerr << "  Error: FFmpeg is not available in PATH." << std::endl;
            std::cerr << "  Install FFmpeg or pass an existing ADX file." << std::endl;
            return false;
        }

        std::string command = "ffmpeg -y -i \"" + inputPath + "\" -vn -c:a adpcm_adx \"" + outputPath + "\"";
        int ret = std::system(command.c_str());
        if (ret != 0) {
            std::cerr << "  Error: FFmpeg failed to encode ADX (exit code " << ret << ")." << std::endl;
            return false;
        }
        return true;
    }

    static void writeLe16(std::vector<char>& out, uint16_t value) {
        out.push_back((char)(value & 0xFF));
        out.push_back((char)((value >> 8) & 0xFF));
    }

    static void writeLe32(std::vector<char>& out, uint32_t value) {
        out.push_back((char)(value & 0xFF));
        out.push_back((char)((value >> 8) & 0xFF));
        out.push_back((char)((value >> 16) & 0xFF));
        out.push_back((char)((value >> 24) & 0xFF));
    }

    static bool convertInputToAdxBytes(const std::string& inputPath, std::vector<char>& adxBytes) {
        std::string inputExt = fs::path(inputPath).extension().string();
        std::transform(inputExt.begin(), inputExt.end(), inputExt.begin(), ::tolower);

        if (inputExt == ".adx") {
            if (!readBinaryFile(inputPath, adxBytes)) {
                return false;
            }
            if (!isAdxFileBuffer(adxBytes)) {
                std::cerr << "  Error: input ADX is invalid: " << inputPath << std::endl;
                return false;
            }
            return true;
        }

        if (inputExt == ".wav" || inputExt == ".mp3") {
            fs::path tempPath = fs::temp_directory_path() / fs::path("cri_tmp_" + std::to_string(std::rand()) + ".adx");
            if (!encodeWithFfmpegToAdx(inputPath, tempPath.string())) {
                return false;
            }

            bool ok = readBinaryFile(tempPath.string(), adxBytes);
            std::error_code ec;
            fs::remove(tempPath, ec);
            if (!ok) {
                return false;
            }
            if (!isAdxFileBuffer(adxBytes)) {
                std::cerr << "  Error: generated ADX is invalid for input: " << inputPath << std::endl;
                return false;
            }
            return true;
        }

        std::cerr << "  Error: unsupported input format (" << inputPath << "). Use .adx, .wav, or .mp3." << std::endl;
        return false;
    }

    static bool buildAwbFromAdxBuffers(const std::vector<std::vector<char>>& adxTracks, const std::string& outputAwbPath) {
        if (adxTracks.empty()) {
            std::cerr << "  Error: no ADX tracks were provided." << std::endl;
            return false;
        }

        constexpr uint32_t alignment = 0x20;
        constexpr uint8_t version = 1;
        constexpr uint8_t offsetSize = 4;
        constexpr uint8_t idSize = 2;

        uint32_t count = (uint32_t)adxTracks.size();
        uint32_t headerBase = 16;
        uint32_t idsSize = count * idSize;
        uint32_t offsetsSize = (count + 1) * offsetSize;
        uint32_t firstDataOffset = headerBase + idsSize + offsetsSize;

        std::vector<uint32_t> offsets(count + 1, 0);
        offsets[0] = firstDataOffset;

        uint32_t cursor = firstDataOffset;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t alignedStart = (cursor % alignment == 0) ? cursor : (cursor + (alignment - (cursor % alignment)));
            offsets[i] = alignedStart;
            cursor = alignedStart + (uint32_t)adxTracks[i].size();
        }
        offsets[count] = cursor;

        std::vector<char> awb;
        awb.reserve(cursor + 64);

        awb.push_back('A');
        awb.push_back('F');
        awb.push_back('S');
        awb.push_back('2');
        awb.push_back((char)version);
        awb.push_back((char)offsetSize);
        awb.push_back((char)idSize);
        awb.push_back(0);
        writeLe32(awb, count);
        writeLe32(awb, alignment);

        for (uint32_t i = 0; i < count; ++i) {
            writeLe16(awb, (uint16_t)i);
        }
        for (uint32_t i = 0; i <= count; ++i) {
            writeLe32(awb, offsets[i]);
        }

        for (uint32_t i = 0; i < count; ++i) {
            while ((uint32_t)awb.size() < offsets[i]) {
                awb.push_back(0);
            }
            awb.insert(awb.end(), adxTracks[i].begin(), adxTracks[i].end());
        }

        if (!writeBinaryFile(outputAwbPath, awb)) {
            return false;
        }

        std::cout << "  AWB generated: " << outputAwbPath << " (" << count << " track(s))" << std::endl;
        return true;
    }

    void decryptAdxTrack(std::vector<char>& trackBuffer, int keyId) {
        (void)applyAdxCipher(trackBuffer, keyId, false, false, true);
    }

    bool convertAdxToWav(const std::vector<char>& adxData, const std::string& wavPath, double renderLoopSeconds = 0.0) {
        DecodedAdx dec;
        if (!decodeAdxPcm(adxData, dec)) {
            return false;
        }
        int channels = dec.channels;
        uint32_t sampleRate = dec.sampleRate;
        uint32_t totalSamples = dec.totalSamples;
        std::vector<int16_t>& pcm = dec.pcm;
        
        std::vector<int16_t> outPcm;
        if (renderLoopSeconds > 0.0) {
            uint32_t targetSamples = (uint32_t)std::llround(renderLoopSeconds * (double)sampleRate);
            if (targetSamples == 0) {
                targetSamples = totalSamples;
            }

            AdxLoopInfo li = parseAdxLoopInfo(adxData);
            if (targetSamples <= totalSamples) {
                outPcm.assign(pcm.begin(), pcm.begin() + (size_t)targetSamples * channels);
            } else if (li.hasLoop && li.loopStartSample < li.loopEndSample && li.loopEndSample <= totalSamples) {
                uint32_t headEnd = li.loopEndSample;
                uint32_t loopLen = li.loopEndSample - li.loopStartSample;
                outPcm.reserve((size_t)targetSamples * channels);

                uint32_t copiedSamples = 0;
                uint32_t firstPart = std::min(targetSamples, headEnd);
                outPcm.insert(outPcm.end(), pcm.begin(), pcm.begin() + (size_t)firstPart * channels);
                copiedSamples += firstPart;

                while (copiedSamples < targetSamples) {
                    uint32_t remain = targetSamples - copiedSamples;
                    uint32_t chunk = std::min(remain, loopLen);
                    size_t srcStart = (size_t)li.loopStartSample * channels;
                    size_t srcEnd = (size_t)(li.loopStartSample + chunk) * channels;
                    outPcm.insert(outPcm.end(), pcm.begin() + srcStart, pcm.begin() + srcEnd);
                    copiedSamples += chunk;
                }
            } else {
                outPcm.reserve((size_t)targetSamples * channels);
                uint32_t copiedSamples = 0;
                while (copiedSamples < targetSamples) {
                    uint32_t remain = targetSamples - copiedSamples;
                    uint32_t chunk = std::min(remain, totalSamples);
                    outPcm.insert(outPcm.end(), pcm.begin(), pcm.begin() + (size_t)chunk * channels);
                    copiedSamples += chunk;
                }
            }
        } else {
            outPcm = std::move(pcm);
        }

        std::ofstream wav(wavPath, std::ios::binary);
        if (!wav) return false;
        
        wav.write("RIFF", 4);
        uint32_t rsize = 36 + (uint32_t)outPcm.size() * 2;
        wav.write((char*)&rsize, 4);
        wav.write("WAVE", 4);
        wav.write("fmt ", 4);
        uint32_t fsize = 16;
        wav.write((char*)&fsize, 4);
        uint16_t format = 1; 
        wav.write((char*)&format, 2);
        uint16_t outChannels = channels;
        wav.write((char*)&outChannels, 2);
        wav.write((char*)&sampleRate, 4);
        uint32_t byteRate = sampleRate * channels * 2;
        wav.write((char*)&byteRate, 4);
        uint16_t blockAlign = channels * 2;
        wav.write((char*)&blockAlign, 2);
        uint16_t bits = 16;
        wav.write((char*)&bits, 2);
        wav.write("data", 4);
        uint32_t dsize = (uint32_t)outPcm.size() * 2;
        wav.write((char*)&dsize, 4);
        wav.write((char*)outPcm.data(), (std::streamsize)outPcm.size() * 2);
        
        return true;
    }

    uint32_t readOffset(std::ifstream& file, uint8_t size) {
        uint32_t value = 0;
        file.read(reinterpret_cast<char*>(&value), size);
        return value;
    }

    bool extractAwbData(std::ifstream& file, uint32_t awbStartOff, const std::string& outputDir, const ExtractionOptions& options) {
        file.seekg(awbStartOff, std::ios::beg);
        
        char header[16];
        if (!file.read(header, 16)) {
            std::cerr << "Error: failed to read AWB archive header." << std::endl;
            return false;
        }

        if (std::memcmp(header, "AFS2", 4) != 0) {
            std::cerr << "Error: invalid AFS2 signature, archive may be corrupted." << std::endl;
            return false;
        }

        uint8_t offsetSize = header[5];
        
        uint32_t count = 0;
        std::memcpy(&count, header + 8, sizeof(uint32_t));
        
        uint32_t alignment = 0;
        std::memcpy(&alignment, header + 12, sizeof(uint32_t));

        if (offsetSize == 0 || count == 0) {
            std::cerr << "Error: AFS2 archive is empty or has an invalid offset field size." << std::endl;
            return false;
        }

        std::cout << "  AFS2 archive at offset 0x" << std::hex << awbStartOff << std::dec << std::endl;
        std::cout << "  Tracks: " << count << std::endl;

        std::vector<uint16_t> ids(count);
        file.read(reinterpret_cast<char*>(ids.data()), count * 2);

        std::vector<uint32_t> offsets(count + 1);
        for (uint32_t i = 0; i <= count; ++i) {
            offsets[i] = readOffset(file, offsetSize);
        }

        std::error_code ec;
        fs::create_directories(outputDir, ec);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t startOff = offsets[i];
            uint32_t endOff = offsets[i + 1];

            if (startOff % alignment != 0) {
                startOff += (alignment - (startOff % alignment));
            }
            
            if (endOff < startOff) continue;
            uint32_t size = endOff - startOff;
            if (size == 0) continue;

            file.seekg(awbStartOff + startOff, std::ios::beg);
            std::vector<char> trackBuffer(size);
            if (!file.read(trackBuffer.data(), size)) {
                std::cerr << "  Error: failed to read track " << ids[i] << std::endl;
                continue;
            }

            bool isAdx = false;
            std::string ext = ".bin";
            if (size >= 4) {
                if (trackBuffer[0] == 'H' && trackBuffer[1] == 'C' && trackBuffer[2] == 'A') {
                    ext = ".hca";
                } else if ((uint8_t)trackBuffer[0] == 0x80 && (uint8_t)trackBuffer[1] == 0x00) {
                    ext = ".adx";
                    isAdx = true;
                } else if (trackBuffer[0] == 'A' && trackBuffer[1] == 'I' && trackBuffer[2] == 'X' && trackBuffer[3] == 'F') {
                    ext = ".aix";
                } else if (trackBuffer[0] == 'R' && trackBuffer[1] == 'I' && trackBuffer[2] == 'F' && trackBuffer[3] == 'F') {
                    ext = ".wav";
                }
            }

            std::cout << "  Extracting  track_" << ids[i] << ext << "  (" << size << " bytes)" << std::endl;

            // Display loop information for ADX files
            if (isAdx) {
                // Parse loop info before potential decryption
                // We need to read metadata from the original encrypted buffer
                // because the loop block is in the unencrypted header area
                uint32_t adxSampleRate = readBe32(trackBuffer, 0x08);
                AdxLoopInfo loopInfo = parseAdxLoopInfo(trackBuffer);
                if (loopInfo.hasLoop && adxSampleRate > 0) {
                    double loopStartSec = (double)loopInfo.loopStartSample / (double)adxSampleRate;
                    double loopEndSec = (double)loopInfo.loopEndSample / (double)adxSampleRate;
                    std::cout << "      Loop: START=" << loopStartSec << "s (sample " << loopInfo.loopStartSample
                              << "), END=" << loopEndSec << "s (sample " << loopInfo.loopEndSample << ")" << std::endl;
                }
            }

            if (isAdx && options.decryptAdx) {
                decryptAdxTrack(trackBuffer, options.adxKeyId);
            }

            if (isAdx && options.convertToWav) {
                std::string wavPath = (fs::path(outputDir) / ("track_" + std::to_string(ids[i]) + ".wav")).string();
                if (convertAdxToWav(trackBuffer, wavPath)) {
                    std::cout << "      Converted to WAV." << std::endl;
                } else {
                    std::cerr << "      Warning: ADX decode failed, writing raw ADX instead." << std::endl;
                    std::string outPath = (fs::path(outputDir) / ("track_" + std::to_string(ids[i]) + ext)).string();
                    std::ofstream outFile(outPath, std::ios::binary);
                    outFile.write(trackBuffer.data(), trackBuffer.size());
                }
            } else {
                std::string outPath = (fs::path(outputDir) / ("track_" + std::to_string(ids[i]) + ext)).string();
                std::ofstream outFile(outPath, std::ios::binary);
                if (outFile) {
                    outFile.write(trackBuffer.data(), trackBuffer.size());
                } else {
                    std::cerr << "  Error: failed to write output file." << std::endl;
                }
            }
        }

        return true;
    }

    bool extractDependencies(const std::string& filepath, const std::string& outputDir, const ExtractionOptions& options) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: cannot open input file: " << filepath << std::endl;
            return false;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize < 4) {
            std::cerr << "Error: file is too small to be a valid container." << std::endl;
            return false;
        }

        char magic[4];
        file.read(magic, 4);

        if (std::memcmp(magic, "AFS2", 4) == 0) {
            std::cout << "  Container: AWB (AFS2)" << std::endl;
            return extractAwbData(file, 0, outputDir, options);
        } else if (std::memcmp(magic, "@UTF", 4) == 0) {
            std::cout << "  Container: ACB (@UTF) — scanning for embedded AFS2 archive..." << std::endl;
            
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(fileSize);
            if (file.read(buffer.data(), fileSize)) {
                const char* target = "AFS2";
                auto it = std::search(buffer.begin(), buffer.end(), target, target + 4);
                
                if (it != buffer.end()) {
                    uint32_t offset = std::distance(buffer.begin(), it);
                    std::cout << "  Embedded AWB located at offset 0x" << std::hex << offset << std::dec << std::endl;
                    return extractAwbData(file, offset, outputDir, options);
                } else {
                    std::cerr << "Error: no embedded AWB archive found inside this ACB file." << std::endl;
                    return false;
                }
            }
        } else {
            std::cerr << "Error: unrecognized container format. Expected AFS2 (AWB) or @UTF (ACB)." << std::endl;
            return false;
        }

        return false;
    }

    bool encryptAdxFile(const std::string& inputPath, const std::string& outputPath, int keyId) {
        std::vector<char> trackBuffer;
        if (!readBinaryFile(inputPath, trackBuffer)) {
            return false;
        }
        if (!isAdxFileBuffer(trackBuffer)) {
            std::cerr << "  Error: Input is not a valid ADX file." << std::endl;
            return false;
        }

        if (!applyAdxCipher(trackBuffer, keyId, true, true, false)) {
            return false;
        }
        if (!writeBinaryFile(outputPath, trackBuffer)) {
            return false;
        }
        std::cout << "  Successfully encrypted ADX to " << outputPath << std::endl;
        return true;
    }

    bool decryptAdxFile(const std::string& inputPath, const std::string& outputPath, int keyId) {
        std::vector<char> trackBuffer;
        if (!readBinaryFile(inputPath, trackBuffer)) {
            return false;
        }
        if (!isAdxFileBuffer(trackBuffer)) {
            std::cerr << "  Error: Input is not a valid ADX file." << std::endl;
            return false;
        }

        if (!applyAdxCipher(trackBuffer, keyId, false, true, false)) {
            return false;
        }
        if (!writeBinaryFile(outputPath, trackBuffer)) {
            return false;
        }
        std::cout << "  Successfully decrypted ADX to " << outputPath << std::endl;
        return true;
    }

    bool adxFileToWav(const std::string& inputPath, const std::string& outputPath, bool decryptFirst, int keyId, double renderLoopSeconds) {
        std::vector<char> trackBuffer;
        if (!readBinaryFile(inputPath, trackBuffer)) {
            return false;
        }
        if (!isAdxFileBuffer(trackBuffer)) {
            std::cerr << "  Error: Input is not a valid ADX file." << std::endl;
            return false;
        }

        if (decryptFirst) {
            if (!applyAdxCipher(trackBuffer, keyId, false, true, false)) {
                return false;
            }
        }

        if (!convertAdxToWav(trackBuffer, outputPath, renderLoopSeconds)) {
            std::cerr << "  Error: failed to decode ADX to WAV." << std::endl;
            return false;
        }
        std::cout << "  Successfully converted ADX to WAV: " << outputPath << std::endl;
        return true;
    }

    bool convertToCriwareAdx(const std::string& inputPath, const std::string& outputPath, bool encryptOutput, int keyId, bool enableLoop, double loopStartSec, double loopEndSec, bool autoLoop) {
        std::string inputExt = fs::path(inputPath).extension().string();
        std::transform(inputExt.begin(), inputExt.end(), inputExt.begin(), ::tolower);

        std::string outputExt = fs::path(outputPath).extension().string();
        std::transform(outputExt.begin(), outputExt.end(), outputExt.begin(), ::tolower);
        if (outputExt != ".adx") {
            std::cerr << "  Error: output file must have .adx extension." << std::endl;
            return false;
        }

        if (inputExt == ".adx") {
            std::vector<char> adxBuffer;
            if (!readBinaryFile(inputPath, adxBuffer)) {
                return false;
            }
            if (!isAdxFileBuffer(adxBuffer)) {
                std::cerr << "  Error: input .adx file is invalid." << std::endl;
                return false;
            }

            if (!writeBinaryFile(outputPath, adxBuffer)) {
                return false;
            }

            if (enableLoop) {
                std::vector<char> patched;
                if (!readBinaryFile(outputPath, patched)) {
                    return false;
                }
                if (!setAdxLoopPoints(patched, loopStartSec, loopEndSec, autoLoop)) {
                    return false;
                }
                if (!writeBinaryFile(outputPath, patched)) {
                    return false;
                }
            }

            if (encryptOutput) {
                return encryptAdxFile(outputPath, outputPath, keyId);
            }

            std::cout << "  ADX copied without encryption: " << outputPath << std::endl;
            return true;
        }

        if (inputExt == ".wav" || inputExt == ".mp3") {
            if (!encodeWithFfmpegToAdx(inputPath, outputPath)) {
                return false;
            }

            if (enableLoop) {
                std::vector<char> patched;
                if (!readBinaryFile(outputPath, patched)) {
                    return false;
                }
                if (!setAdxLoopPoints(patched, loopStartSec, loopEndSec, autoLoop)) {
                    return false;
                }
                if (!writeBinaryFile(outputPath, patched)) {
                    return false;
                }
            }

            if (encryptOutput) {
                return encryptAdxFile(outputPath, outputPath, keyId);
            }

            std::cout << "  ADX encoded without encryption: " << outputPath << std::endl;
            return true;
        }

        std::cerr << "  Error: unsupported input format. Use .adx, .wav, or .mp3." << std::endl;
        return false;
    }

    bool buildAwbFromInputs(const std::vector<std::string>& inputPaths, const std::string& outputAwbPath, bool encryptOutput, int keyId, bool enableLoop, double loopStartSec, double loopEndSec, bool autoLoop) {
        if (inputPaths.empty()) {
            std::cerr << "  Error: no inputs provided for AWB generation." << std::endl;
            return false;
        }

        std::vector<std::vector<char>> tracks;
        tracks.reserve(inputPaths.size());

        for (const std::string& input : inputPaths) {
            std::vector<char> adxBytes;
            if (!convertInputToAdxBytes(input, adxBytes)) {
                return false;
            }

            if (enableLoop) {
                if (!setAdxLoopPoints(adxBytes, loopStartSec, loopEndSec, autoLoop)) {
                    return false;
                }
            }

            if (encryptOutput) {
                if (!applyAdxCipher(adxBytes, keyId, true, false, false)) {
                    return false;
                }
            }
            tracks.push_back(std::move(adxBytes));
        }

        return buildAwbFromAdxBuffers(tracks, outputAwbPath);
    }

    bool buildAcbAwbFromTemplate(const std::string& templateAcbPath, const std::vector<std::string>& inputPaths, const std::string& outputAcbPath, const std::string& outputAwbPath, bool encryptOutput, int keyId, bool enableLoop, double loopStartSec, double loopEndSec, bool autoLoop) {
        if (!buildAwbFromInputs(inputPaths, outputAwbPath, encryptOutput, keyId, enableLoop, loopStartSec, loopEndSec, autoLoop)) {
            return false;
        }

        std::vector<char> templateAcb;
        if (!readBinaryFile(templateAcbPath, templateAcb)) {
            return false;
        }
        if (templateAcb.size() < 4 || std::memcmp(templateAcb.data(), "@UTF", 4) != 0) {
            std::cerr << "  Error: template ACB is not a valid @UTF file." << std::endl;
            return false;
        }

        std::vector<char> awb;
        if (!readBinaryFile(outputAwbPath, awb)) {
            return false;
        }

        size_t afs2Pos = std::string::npos;
        for (size_t i = 0; i + 4 <= templateAcb.size(); ++i) {
            if (templateAcb[i] == 'A' && templateAcb[i + 1] == 'F' && templateAcb[i + 2] == 'S' && templateAcb[i + 3] == '2') {
                afs2Pos = i;
            }
        }
        if (afs2Pos == std::string::npos) {
            std::cerr << "  Error: template ACB does not contain StreamAwbAfs2Header (AFS2)." << std::endl;
            return false;
        }

        // Patch embedded StreamAwbAfs2Header blob with generated AWB header bytes.
        size_t patchLen = std::min<size_t>(32, std::min(awb.size(), templateAcb.size() - afs2Pos));
        std::memcpy(templateAcb.data() + afs2Pos, awb.data(), patchLen);

        if (!writeBinaryFile(outputAcbPath, templateAcb)) {
            return false;
        }

        std::cout << "  ACB generated from template: " << outputAcbPath << std::endl;
        std::cout << "  Note: cue metadata comes from template ACB; only stream AWB header blob is patched." << std::endl;
        return true;
    }

    // ========== Container Info Functions ==========

    static void parseAdxTrackInfo(const std::vector<char>& trackBuffer, TrackInfo& track) {
        if (trackBuffer.size() < 24) return;
        
        track.format = "ADX";
        track.sampleRate = readBe32(trackBuffer, 0x08);
        track.totalSamples = readBe32(trackBuffer, 0x0C);
        track.channels = (uint8_t)trackBuffer[0x07];
        track.fileSize = (uint32_t)trackBuffer.size();
        
        if (track.sampleRate > 0) {
            track.durationSec = (double)track.totalSamples / (double)track.sampleRate;
        }
        
        AdxLoopInfo loopInfo = parseAdxLoopInfo(trackBuffer);
        track.hasLoop = loopInfo.hasLoop;
        track.loopStartSample = loopInfo.loopStartSample;
        track.loopEndSample = loopInfo.loopEndSample;
        
        if (track.hasLoop && track.sampleRate > 0) {
            track.loopStartSec = (double)track.loopStartSample / (double)track.sampleRate;
            track.loopEndSec = (double)track.loopEndSample / (double)track.sampleRate;
        }
    }

    static bool getAwbContainerInfo(std::ifstream& file, uint32_t awbStartOff, ContainerInfo& info) {
        file.seekg(awbStartOff, std::ios::beg);
        
        char header[16];
        if (!file.read(header, 16)) {
            return false;
        }

        if (std::memcmp(header, "AFS2", 4) != 0) {
            return false;
        }

        uint8_t offsetSize = header[5];
        uint32_t count = 0;
        std::memcpy(&count, header + 8, sizeof(uint32_t));
        uint32_t alignment = 0;
        std::memcpy(&alignment, header + 12, sizeof(uint32_t));

        if (offsetSize == 0 || count == 0) {
            return false;
        }

        info.trackCount = count;

        std::vector<uint16_t> ids(count);
        file.read(reinterpret_cast<char*>(ids.data()), count * 2);

        std::vector<uint32_t> offsets(count + 1);
        for (uint32_t i = 0; i <= count; ++i) {
            uint32_t value = 0;
            file.read(reinterpret_cast<char*>(&value), offsetSize);
            offsets[i] = value;
        }

        info.tracks.resize(count);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t startOff = offsets[i];
            uint32_t endOff = offsets[i + 1];

            if (startOff % alignment != 0) {
                startOff += (alignment - (startOff % alignment));
            }
            
            if (endOff < startOff) continue;
            uint32_t size = endOff - startOff;
            if (size == 0) continue;

            file.seekg(awbStartOff + startOff, std::ios::beg);
            std::vector<char> trackBuffer(size);
            if (!file.read(trackBuffer.data(), size)) {
                continue;
            }

            TrackInfo& track = info.tracks[i];
            track.id = ids[i];
            track.fileSize = size;

            if (size >= 4) {
                if (trackBuffer[0] == 'H' && trackBuffer[1] == 'C' && trackBuffer[2] == 'A') {
                    track.format = "HCA";
                } else if ((uint8_t)trackBuffer[0] == 0x80 && (uint8_t)trackBuffer[1] == 0x00) {
                    parseAdxTrackInfo(trackBuffer, track);
                } else if (trackBuffer[0] == 'A' && trackBuffer[1] == 'I' && trackBuffer[2] == 'X') {
                    track.format = "AIX";
                } else if (trackBuffer[0] == 'R' && trackBuffer[1] == 'I' && trackBuffer[2] == 'F') {
                    track.format = "WAV";
                } else {
                    track.format = "UNKNOWN";
                }
            }
        }

        return true;
    }

    // Parse ACB @UTF table to extract track names from CueName table
    static void parseAcbMetadata(const std::vector<char>& acbData, ContainerInfo& info) {
        if (acbData.size() < 8 || std::memcmp(acbData.data(), "@UTF", 4) != 0) {
            return;
        }

        // Search for "CueName" table blob within ACB
        const char* cuenamePattern = "CueName";
        auto it = std::search(acbData.begin(), acbData.end(), cuenamePattern, cuenamePattern + 7);
        
        if (it != acbData.end()) {
            // CueName table found - extract names
            // For simplicity, we'll just note that CueName table exists
            // Full parsing would require complete @UTF table decoder
        }

        // Search for WaveformExtensionData to get loop points from ACB
        // Pattern: "LoopStart" followed by constant values
        const char* loopStartPattern = "LoopStart";
        auto loopIt = std::search(acbData.begin(), acbData.end(), loopStartPattern, loopStartPattern + 9);
        
        if (loopIt != acbData.end()) {
            // Found LoopStart string - the values should be nearby in the constant data area
            // This is a heuristic approach; proper parsing would need full @UTF decoder
        }

        // Look for "Name" field in root @UTF table
        const char* namePattern = "Name\x00";
        auto nameIt = std::search(acbData.begin(), acbData.end(), namePattern, namePattern + 5);
        if (nameIt != acbData.end()) {
            // Could extract ACB name here with proper @UTF parsing
        }
    }

    bool getContainerInfo(const std::string& filepath, ContainerInfo& info) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize < 4) {
            return false;
        }

        char magic[4];
        file.read(magic, 4);

        if (std::memcmp(magic, "AFS2", 4) == 0) {
            info.containerType = "AWB";
            info.awbPath = filepath;
            return getAwbContainerInfo(file, 0, info);
        } else if (std::memcmp(magic, "@UTF", 4) == 0) {
            // ACB file - check for embedded AWB or external AWB
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(fileSize);
            if (!file.read(buffer.data(), fileSize)) {
                return false;
            }

            // Parse ACB metadata
            parseAcbMetadata(buffer, info);
            info.acbPath = filepath;

            // First check for external AWB file (this is the most common case)
            fs::path acbPath(filepath);
            fs::path awbPath = acbPath.parent_path() / (acbPath.stem().string() + ".awb");
            
            if (fs::exists(awbPath)) {
                info.containerType = "ACB + external AWB";
                info.awbPath = awbPath.string();
                
                std::ifstream awbFile(awbPath.string(), std::ios::binary);
                if (awbFile.is_open()) {
                    return getAwbContainerInfo(awbFile, 0, info);
                }
            }

            // No external AWB - search for embedded AFS2 with actual audio data
            // Note: ACB files may contain StreamAwbAfs2Header (32 byte mini-header) 
            // which references external AWB, not actual embedded audio
            const char* target = "AFS2";
            auto it = std::search(buffer.begin(), buffer.end(), target, target + 4);
            
            if (it != buffer.end()) {
                uint32_t offset = (uint32_t)std::distance(buffer.begin(), it);
                
                // Check if this AFS2 has enough data to be real audio (not just a header reference)
                // A StreamAwbAfs2Header is typically ~32 bytes, real AWB with audio is much larger
                if (buffer.size() - offset > 256) {
                    info.containerType = "ACB (embedded AWB)";
                    
                    std::ifstream file2(filepath, std::ios::binary);
                    if (!file2.is_open()) return false;
                    return getAwbContainerInfo(file2, offset, info);
                }
            }

            info.containerType = "ACB (no AWB found)";
            return true;
        }

        return false;
    }

    bool showFileInfo(const std::string& filepath) {
        ContainerInfo info;
        if (!getContainerInfo(filepath, info)) {
            std::cerr << "Error: could not read container info from " << filepath << std::endl;
            return false;
        }

        std::cout << std::endl;
        std::cout << "=== CRIWARE Container Info ===" << std::endl;
        std::cout << "Type: " << info.containerType << std::endl;
        if (!info.acbPath.empty()) {
            std::cout << "ACB: " << info.acbPath << std::endl;
        }
        if (!info.awbPath.empty()) {
            std::cout << "AWB: " << info.awbPath << std::endl;
        }
        std::cout << "Track count: " << info.trackCount << std::endl;
        std::cout << std::endl;

        if (!info.tracks.empty()) {
            std::cout << "=== Tracks ===" << std::endl;
            for (const auto& track : info.tracks) {
                std::cout << std::endl;
                std::cout << "Track " << track.id << ":" << std::endl;
                if (!track.name.empty()) {
                    std::cout << "  Name: " << track.name << std::endl;
                }
                std::cout << "  Format: " << track.format << std::endl;
                std::cout << "  Size: " << track.fileSize << " bytes" << std::endl;
                
                if (track.sampleRate > 0) {
                    std::cout << "  Sample rate: " << track.sampleRate << " Hz" << std::endl;
                    std::cout << "  Channels: " << track.channels << std::endl;
                    std::cout << "  Total samples: " << track.totalSamples << std::endl;
                    std::cout << "  Duration: " << std::fixed << std::setprecision(3) << track.durationSec << " sec" << std::endl;
                }
                
                if (track.hasLoop) {
                    std::cout << "  Loop: YES" << std::endl;
                    std::cout << "    Start: " << std::fixed << std::setprecision(3) << track.loopStartSec 
                              << " sec (sample " << track.loopStartSample << ")" << std::endl;
                    std::cout << "    End: " << std::fixed << std::setprecision(3) << track.loopEndSec 
                              << " sec (sample " << track.loopEndSample << ")" << std::endl;
                } else {
                    std::cout << "  Loop: NO" << std::endl;
                }
            }
        }

        return true;
    }

    bool extractWithMetadata(const std::string& filepath, const std::string& outputDir, const ExtractionOptions& options) {
        ContainerInfo info;
        if (!getContainerInfo(filepath, info)) {
            std::cerr << "Error: could not read container info." << std::endl;
            return false;
        }

        // Extract files normally
        if (!extractDependencies(filepath, outputDir, options)) {
            return false;
        }

        // Write metadata JSON
        std::string metadataPath = (fs::path(outputDir) / "metadata.json").string();
        std::ofstream metaFile(metadataPath);
        if (!metaFile) {
            std::cerr << "Warning: could not write metadata.json" << std::endl;
            return true; // Extraction succeeded, just metadata failed
        }

        metaFile << "{\n";
        metaFile << "  \"containerType\": \"" << info.containerType << "\",\n";
        metaFile << "  \"trackCount\": " << info.trackCount << ",\n";
        metaFile << "  \"tracks\": [\n";

        for (size_t i = 0; i < info.tracks.size(); ++i) {
            const auto& track = info.tracks[i];
            metaFile << "    {\n";
            metaFile << "      \"id\": " << track.id << ",\n";
            metaFile << "      \"format\": \"" << track.format << "\",\n";
            metaFile << "      \"sampleRate\": " << track.sampleRate << ",\n";
            metaFile << "      \"channels\": " << track.channels << ",\n";
            metaFile << "      \"totalSamples\": " << track.totalSamples << ",\n";
            metaFile << "      \"duration\": " << std::fixed << std::setprecision(6) << track.durationSec << ",\n";
            metaFile << "      \"hasLoop\": " << (track.hasLoop ? "true" : "false") << ",\n";
            metaFile << "      \"loopStartSample\": " << track.loopStartSample << ",\n";
            metaFile << "      \"loopEndSample\": " << track.loopEndSample << ",\n";
            metaFile << "      \"loopStartSec\": " << std::fixed << std::setprecision(6) << track.loopStartSec << ",\n";
            metaFile << "      \"loopEndSec\": " << std::fixed << std::setprecision(6) << track.loopEndSec << "\n";
            metaFile << "    }" << (i + 1 < info.tracks.size() ? "," : "") << "\n";
        }

        metaFile << "  ]\n";
        metaFile << "}\n";

        std::cout << "  Metadata saved to: " << metadataPath << std::endl;
        return true;
    }

    bool listTracks(const std::string& filepath) {
        ContainerInfo info;
        if (!getContainerInfo(filepath, info)) {
            std::cerr << "Error: could not read container info." << std::endl;
            return false;
        }

        std::cout << "\nAvailable tracks in " << filepath << ":\n";
        for (size_t i = 0; i < info.tracks.size(); ++i) {
            const auto& t = info.tracks[i];
            std::cout << "  [" << i << "] Track " << t.id << " - " << t.format;
            if (t.durationSec > 0) {
                std::cout << " (" << std::fixed << std::setprecision(1) << t.durationSec << "s";
                if (t.hasLoop) {
                    std::cout << ", loops at " << std::fixed << std::setprecision(1) << t.loopStartSec << "s";
                }
                std::cout << ")";
            }
            std::cout << "\n";
        }
        return true;
    }

#ifdef _WIN32
    // Windows audio player implementation
    struct AudioPlayerState {
        std::atomic<bool> playing{false};
        std::atomic<bool> paused{false};
        std::atomic<bool> stopRequested{false};
        std::atomic<uint32_t> currentSample{0};
        uint32_t totalSamples = 0;
        uint32_t loopStartSample = 0;
        uint32_t loopEndSample = 0;
        bool hasLoop = false;
        uint32_t sampleRate = 0;
        int channels = 0;
    };

    static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        (void)hwo; (void)dwParam1; (void)dwParam2;
        if (uMsg == WOM_DONE) {
            AudioPlayerState* state = reinterpret_cast<AudioPlayerState*>(dwInstance);
            if (state) {
                state->playing = false;
            }
        }
    }

    static void displayProgress(AudioPlayerState& state) {
        const int barWidth = 50;
        
        while (state.playing && !state.stopRequested) {
            uint32_t current = state.currentSample.load();
            double currentSec = (double)current / (double)state.sampleRate;
            double totalSec = (double)state.totalSamples / (double)state.sampleRate;
            
            // Calculate progress position
            double progress = (state.totalSamples > 0) ? (double)current / (double)state.totalSamples : 0.0;
            int pos = (int)(progress * barWidth);
            
            // Build progress bar
            std::string bar = "[";
            for (int i = 0; i < barWidth; ++i) {
                if (state.hasLoop && state.loopStartSample > 0) {
                    double loopPos = (double)state.loopStartSample / (double)state.totalSamples * barWidth;
                    if (i == (int)loopPos) {
                        bar += "|";  // Loop marker
                        continue;
                    }
                }
                if (i < pos) bar += "=";
                else if (i == pos) bar += ">";
                else bar += " ";
            }
            bar += "]";
            
            // Status indicator
            std::string status = state.paused ? "PAUSED" : "PLAYING";
            
            // Print progress
            std::cout << "\r" << bar << " " << std::fixed << std::setprecision(1) 
                      << currentSec << "/" << totalSec << "s " << status << "    " << std::flush;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << std::endl;
    }

    bool playAudio(const std::string& filepath, int trackIndex) {
        ContainerInfo info;
        if (!getContainerInfo(filepath, info)) {
            std::cerr << "Error: could not read container info." << std::endl;
            return false;
        }

        if (info.tracks.empty()) {
            std::cerr << "Error: no tracks found in container." << std::endl;
            return false;
        }

        // Track selection
        size_t selectedTrack = 0;
        if (trackIndex < 0) {
            listTracks(filepath);
            if (info.tracks.size() > 1) {
                std::cout << "\nSelect track [0-" << (info.tracks.size() - 1) << "]: ";
                std::cin >> selectedTrack;
                if (selectedTrack >= info.tracks.size()) {
                    std::cerr << "Invalid track selection." << std::endl;
                    return false;
                }
            }
        } else {
            selectedTrack = (size_t)trackIndex;
            if (selectedTrack >= info.tracks.size()) {
                std::cerr << "Invalid track index." << std::endl;
                return false;
            }
        }

        const TrackInfo& track = info.tracks[selectedTrack];
        
        if (track.format != "ADX") {
            std::cerr << "Error: only ADX playback is currently supported." << std::endl;
            return false;
        }

        // Load track data
        std::string awbPath = info.awbPath.empty() ? filepath : info.awbPath;
        std::ifstream file(awbPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: cannot open " << awbPath << std::endl;
            return false;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        char magic[4];
        file.read(magic, 4);
        if (std::memcmp(magic, "AFS2", 4) != 0) {
            std::cerr << "Error: invalid AWB format." << std::endl;
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::vector<char> awbData(fileSize);
        file.read(awbData.data(), fileSize);
        file.close();

        // Parse AFS2 to get track data
        uint8_t offsetSize = awbData[5];
        uint32_t count = 0;
        std::memcpy(&count, awbData.data() + 8, sizeof(uint32_t));
        uint32_t alignment = 0;
        std::memcpy(&alignment, awbData.data() + 12, sizeof(uint32_t));

        size_t idsStart = 16;
        size_t offsetsStart = idsStart + count * 2;

        auto readOffset = [&](size_t idx) -> uint32_t {
            uint32_t val = 0;
            std::memcpy(&val, awbData.data() + offsetsStart + idx * offsetSize, offsetSize);
            return val;
        };

        uint32_t trackStart = readOffset(selectedTrack);
        uint32_t trackEnd = readOffset(selectedTrack + 1);
        if (trackStart % alignment != 0) {
            trackStart += (alignment - (trackStart % alignment));
        }

        std::vector<char> adxData(awbData.begin() + trackStart, awbData.begin() + trackEnd);

        // Decode ADX to PCM
        DecodedAdx decoded;
        if (!decodeAdxPcm(adxData, decoded)) {
            std::cerr << "Error: failed to decode ADX." << std::endl;
            return false;
        }

        // Setup player state
        AudioPlayerState state;
        state.totalSamples = decoded.totalSamples;
        state.sampleRate = decoded.sampleRate;
        state.channels = decoded.channels;
        state.hasLoop = track.hasLoop;
        state.loopStartSample = track.loopStartSample;
        state.loopEndSample = track.loopEndSample;

        // Setup waveOut
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = (WORD)decoded.channels;
        wfx.nSamplesPerSec = decoded.sampleRate;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        HWAVEOUT hwo = nullptr;
        MMRESULT result = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, (DWORD_PTR)waveOutCallback, (DWORD_PTR)&state, CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            std::cerr << "Error: failed to open audio device." << std::endl;
            return false;
        }

        std::cout << "\n=== Audio Player ===" << std::endl;
        std::cout << "Track: " << track.id << " (" << track.format << ")" << std::endl;
        std::cout << "Duration: " << std::fixed << std::setprecision(2) << track.durationSec << " sec" << std::endl;
        if (track.hasLoop) {
            std::cout << "Loop: " << std::fixed << std::setprecision(2) << track.loopStartSec << "s - " << track.loopEndSec << "s" << std::endl;
        }
        std::cout << "\nControls: [Space] Pause/Resume | [Q] Quit | [R] Restart" << std::endl;
        std::cout << "          | marks loop start point" << std::endl;
        std::cout << std::endl;

        // Prepare audio buffers
        const size_t bufferSamples = state.sampleRate / 4;  // 250ms buffers
        const size_t bufferSize = bufferSamples * decoded.channels * sizeof(int16_t);
        
        std::vector<WAVEHDR> headers(4);
        std::vector<std::vector<int16_t>> buffers(4);
        for (auto& buf : buffers) {
            buf.resize(bufferSamples * decoded.channels);
        }

        state.playing = true;
        state.currentSample = 0;

        // Start progress display thread
        std::thread progressThread(displayProgress, std::ref(state));

        size_t bufferIdx = 0;
        uint32_t samplePos = 0;
        bool looping = track.hasLoop;

        while (!state.stopRequested) {
            // Check for keyboard input
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 'q' || ch == 'Q' || ch == 27) {  // Q or Escape
                    state.stopRequested = true;
                    break;
                } else if (ch == ' ') {  // Space - pause/resume
                    state.paused = !state.paused;
                    if (state.paused) {
                        waveOutPause(hwo);
                    } else {
                        waveOutRestart(hwo);
                    }
                } else if (ch == 'r' || ch == 'R') {  // R - restart
                    waveOutReset(hwo);
                    samplePos = 0;
                    state.currentSample = 0;
                }
            }

            if (state.paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Fill buffer
            auto& buf = buffers[bufferIdx];
            auto& hdr = headers[bufferIdx];

            size_t samplesToWrite = bufferSamples;
            if (samplePos + samplesToWrite > decoded.totalSamples) {
                samplesToWrite = decoded.totalSamples - samplePos;
            }

            if (samplesToWrite == 0) {
                if (looping && track.loopStartSample < track.loopEndSample) {
                    samplePos = track.loopStartSample;
                    continue;
                } else {
                    break;  // End of track
                }
            }

            // Copy PCM data
            size_t srcOffset = samplePos * decoded.channels;
            std::memcpy(buf.data(), decoded.pcm.data() + srcOffset, samplesToWrite * decoded.channels * sizeof(int16_t));

            // Zero rest if partial buffer
            if (samplesToWrite < bufferSamples) {
                std::memset(buf.data() + samplesToWrite * decoded.channels, 0, 
                           (bufferSamples - samplesToWrite) * decoded.channels * sizeof(int16_t));
            }

            std::memset(&hdr, 0, sizeof(WAVEHDR));
            hdr.lpData = reinterpret_cast<LPSTR>(buf.data());
            hdr.dwBufferLength = (DWORD)(samplesToWrite * decoded.channels * sizeof(int16_t));

            waveOutPrepareHeader(hwo, &hdr, sizeof(WAVEHDR));
            waveOutWrite(hwo, &hdr, sizeof(WAVEHDR));

            samplePos += (uint32_t)samplesToWrite;
            state.currentSample = samplePos;

            // Check for loop point
            if (looping && samplePos >= track.loopEndSample) {
                samplePos = track.loopStartSample;
            }

            // Wait for buffer to finish
            while ((hdr.dwFlags & WHDR_DONE) == 0 && !state.stopRequested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            waveOutUnprepareHeader(hwo, &hdr, sizeof(WAVEHDR));
            bufferIdx = (bufferIdx + 1) % buffers.size();
        }

        state.playing = false;
        if (progressThread.joinable()) {
            progressThread.join();
        }

        waveOutReset(hwo);
        waveOutClose(hwo);

        std::cout << "\nPlayback stopped." << std::endl;
        return true;
    }

#else
    // Non-Windows placeholder
    bool playAudio(const std::string& filepath, int trackIndex) {
        (void)filepath; (void)trackIndex;
        std::cerr << "Error: audio playback is only supported on Windows." << std::endl;
        return false;
    }
#endif

} // namespace CriAudio
