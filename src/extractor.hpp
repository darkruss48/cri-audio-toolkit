#pragma once

#include <string>

namespace CriAudio {

    struct ExtractionOptions {
        bool decryptAdx = false;
        int adxKeyId = 9;       // Clé par défaut (9 = Dragon Ball)
        bool convertToWav = false;
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

} // namespace CriAudio
