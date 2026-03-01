#include "extractor.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace CriAudio {

    struct AdxKey {
        int start;
        int mult;
        int add;
        int type9;
        const char* name;
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

    void decryptAdxTrack(std::vector<char>& trackBuffer, int keyId) {
        if (keyId < 0 || keyId >= 10) {
            std::cerr << "  Error: invalid ADX key ID (" << keyId << ")" << std::endl;
            return;
        }
        const AdxKey& k = KEYS[keyId];
        
        if (trackBuffer.size() < 24) return;
        
        bool isEncrypted = false;
        if (trackBuffer.size() >= 0x14) {
            uint8_t encFlag = trackBuffer[0x13];
            // 8: standard adx encrypt, 9: type 9
            if (encFlag == 8 || encFlag == 9) {
                isEncrypted = true;
                trackBuffer[0x13] = 0; // Clear flag
            }
        }

        if (!isEncrypted) return;

        int startoff = (((uint8_t)trackBuffer[2] << 8) | (uint8_t)trackBuffer[3]) + 4;
        int channels = trackBuffer[7];
        uint32_t totalSamples = ((uint8_t)trackBuffer[12] << 24) | ((uint8_t)trackBuffer[13] << 16) | ((uint8_t)trackBuffer[14] << 8) | (uint8_t)trackBuffer[15];
        
        int blocks = (totalSamples + 31) / 32;
        int endoff = blocks * 18 * channels + startoff;
        
        if ((size_t)endoff > trackBuffer.size()) {
            endoff = trackBuffer.size();
        }

        int mask = k.type9 ? 0x1FFF : 0x7FFF;
        int xor_val = k.start;
        
        for (int off = startoff; off <= endoff - 18; off += 18) {
            int val = ((uint8_t)trackBuffer[off] << 8) | (uint8_t)trackBuffer[off+1];
            val = (val ^ xor_val) & mask;
            trackBuffer[off] = (val >> 8) & 0xFF;
            trackBuffer[off+1] = val & 0xFF;
            
            xor_val = (xor_val * k.mult + k.add) & 0x7FFF;
        }
        std::cout << "      ADX decrypted  [key " << keyId << ": " << k.name << "]" << std::endl;
    }

    bool convertAdxToWav(const std::vector<char>& adxData, const std::string& wavPath) {
        if (adxData.size() < 24) return false;
        
        uint32_t startoff = (((uint8_t)adxData[2] << 8) | (uint8_t)adxData[3]) + 4;
        int channels = adxData[7];
        uint32_t sampleRate = ((uint8_t)adxData[8] << 24) | ((uint8_t)adxData[9] << 16) | ((uint8_t)adxData[10] << 8) | (uint8_t)adxData[11];
        uint32_t totalSamples = ((uint8_t)adxData[12] << 24) | ((uint8_t)adxData[13] << 16) | ((uint8_t)adxData[14] << 8) | (uint8_t)adxData[15];
        uint16_t highpass = ((uint8_t)adxData[16] << 8) | (uint8_t)adxData[17];
        
        if (channels > 2 || channels == 0) {
            std::cerr << "  Error: unsupported channel count (" << channels << "), only mono/stereo ADX is supported." << std::endl;
            return false;
        }

        double PI_VAL = 3.14159265358979323846;
        double a = std::sqrt(2.0) - std::cos(2.0 * PI_VAL * highpass / sampleRate);
        double b = std::sqrt(2.0) - 1.0;
        double c = (a - std::sqrt((a + b) * (a - b))) / b;
        double coef1 = c * 2.0;
        double coef2 = -(c * c);
        
        std::vector<int16_t> pcm(totalSamples * channels);
        int sampleIdx[2] = {0, 0};
        int hist1[2] = {0, 0};
        int hist2[2] = {0, 0};
        
        int inOffset = startoff;
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
                
                pcm[sampleIdx[ch] * channels + ch] = (int16_t)intSample;
                hist2[ch] = hist1[ch];
                hist1[ch] = intSample;
                sampleIdx[ch]++;
            }
        }
        
        std::ofstream wav(wavPath, std::ios::binary);
        if (!wav) return false;
        
        wav.write("RIFF", 4);
        uint32_t rsize = 36 + pcm.size() * 2;
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
        uint32_t dsize = pcm.size() * 2;
        wav.write((char*)&dsize, 4);
        wav.write((char*)pcm.data(), pcm.size() * 2);
        
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

} // namespace CriAudio
