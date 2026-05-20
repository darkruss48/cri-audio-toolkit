#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace CriAudio {

    struct ExtractionOptions {
        bool decryptAdx = false;
        int adxKeyId = 9;       // Clé par défaut (9 = Dragon Ball)
        bool convertToWav = false;
    };

    // Information about a single audio track
    struct TrackInfo {
        uint16_t id = 0;
        std::string name;           // From ACB CueName table (if available)
        std::string format;         // "ADX", "HCA", etc.
        uint32_t sampleRate = 0;
        int channels = 0;
        uint32_t totalSamples = 0;
        double durationSec = 0.0;
        bool hasLoop = false;
        uint32_t loopStartSample = 0;
        uint32_t loopEndSample = 0;
        double loopStartSec = 0.0;
        double loopEndSec = 0.0;
        uint32_t fileSize = 0;
    };

    // Information about an ACB/AWB container
    struct ContainerInfo {
        std::string containerType;  // "AWB", "ACB", "ACB+AWB"
        std::string acbPath;
        std::string awbPath;
        uint32_t trackCount = 0;
        std::vector<TrackInfo> tracks;
        // ACB-specific metadata
        std::string acbName;        // From ACB Name field
        uint32_t acbVersion = 0;
    };

    /**
     * @brief Extracts audio files (HCA, ADX) from an AWB or ACB file.
     * 
     * @param filepath The path to the .awb or .acb file.
     * @param outputDir Directory where the audio tracks will be extracted.
     * @param options Options for extraction (ADX decrypt, WAV convert).
     * @return true if successful, false otherwise.
     */
    bool extractDependencies(const std::string& filepath, const std::string& outputDir, const ExtractionOptions& options);

    /**
     * @brief Encrypts an ADX file using a specified key.
     */
    bool encryptAdxFile(const std::string& inputPath, const std::string& outputPath, int keyId = 9);

    /**
     * @brief Decrypts an ADX file using a specified key.
     */
    bool decryptAdxFile(const std::string& inputPath, const std::string& outputPath, int keyId = 9);

    /**
     * @brief Converts an ADX file to WAV. Optional inline decrypt before decode.
     */
    bool adxFileToWav(const std::string& inputPath, const std::string& outputPath, bool decryptFirst = false, int keyId = 9, double renderLoopSeconds = 0.0);

    /**
     * @brief Converts ADX/WAV/MP3 to ADX (CRIWARE), with optional ADX encryption.
     */
    bool convertToCriwareAdx(const std::string& inputPath, const std::string& outputPath, bool encryptOutput = true, int keyId = 9, bool enableLoop = false, double loopStartSec = 0.0, double loopEndSec = -1.0, bool autoLoop = false);

    /**
     * @brief Builds an AFS2 AWB from ADX/WAV/MP3 inputs.
     */
    bool buildAwbFromInputs(const std::vector<std::string>& inputPaths, const std::string& outputAwbPath, bool encryptOutput = true, int keyId = 9, bool enableLoop = false, double loopStartSec = 0.0, double loopEndSec = -1.0, bool autoLoop = false);

    /**
     * @brief Builds an AWB and clones a template ACB with patched embedded AFS2 header.
     */
    bool buildAcbAwbFromTemplate(const std::string& templateAcbPath, const std::vector<std::string>& inputPaths, const std::string& outputAcbPath, const std::string& outputAwbPath, bool encryptOutput = true, int keyId = 9, bool enableLoop = false, double loopStartSec = 0.0, double loopEndSec = -1.0, bool autoLoop = false);

    /**
     * @brief Gets detailed information about an ACB/AWB container.
     */
    bool getContainerInfo(const std::string& filepath, ContainerInfo& info);

    /**
     * @brief Displays information about an ACB/AWB container.
     */
    bool showFileInfo(const std::string& filepath);

    /**
     * @brief Extracts tracks with metadata JSON for round-trip rebuilding.
     */
    bool extractWithMetadata(const std::string& filepath, const std::string& outputDir, const ExtractionOptions& options);

    /**
     * @brief Lists tracks in a container for interactive selection.
     */
    bool listTracks(const std::string& filepath);

    /**
     * @brief Plays audio from ACB/AWB with progress bar.
     * @param filepath Path to ACB or AWB file
     * @param trackIndex Track index to play (-1 for interactive selection)
     */
    bool playAudio(const std::string& filepath, int trackIndex = -1);

} // namespace CriAudio
