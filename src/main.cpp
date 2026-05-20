#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "extractor.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::cout << "CriAudioExtractor — CRI Middleware ACB/AWB demuxer" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    auto parseKeyArg = [](const char* value, int& outKey) -> bool {
        try {
            outKey = std::stoi(value);
            return outKey >= 0 && outKey <= 9;
        } catch (...) {
            return false;
        }
    };

    auto parseDoubleArg = [](const char* value, double& out) -> bool {
        try {
            size_t used = 0;
            out = std::stod(value, &used);
            return used == std::string(value).size();
        } catch (...) {
            return false;
        }
    };

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file.acb|input_file.awb> [output_directory] [options...]" << std::endl;
        std::cerr << "       " << argv[0] << " --info <input_file.acb|input_file.awb>" << std::endl;
        std::cerr << "       " << argv[0] << " --play <input_file.acb|input_file.awb> [track_index]" << std::endl;
        std::cerr << "       " << argv[0] << " --encrypt-adx <input> <output>" << std::endl;
        std::cerr << "       " << argv[0] << " --decrypt-adx-file <key_id> <input.adx> <output.adx>" << std::endl;
        std::cerr << "       " << argv[0] << " --adx-to-wav <input.adx> <output.wav> [--decrypt-key <key_id>] [--loop-seconds <sec>]" << std::endl;
        std::cerr << "       " << argv[0] << " --to-criware <input.adx|input.wav|input.mp3> <output.adx> [--key <key_id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]" << std::endl;
        std::cerr << "       " << argv[0] << " --make-awb <output.awb> <input1> [input2 ...] [--key <key_id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]" << std::endl;
        std::cerr << "       " << argv[0] << " --make-acb-awb <template.acb> <output.acb> <output.awb> <input1> [input2 ...] [--key <key_id>] [--no-encrypt] [--auto-loop] [--loop-start-sec <s>] [--loop-end-sec <s>]" << std::endl;
        std::cerr << "\nOptions:" << std::endl;
        std::cerr << "  --info <file>            Show detailed info about ACB/AWB container (tracks, loops, duration)" << std::endl;
        std::cerr << "  --play <file> [track]    Play audio from ACB/AWB with progress bar" << std::endl;
        std::cerr << "  --decrypt-adx <key_id>   Decrypt ADX files using provided key id (e.g. 9)" << std::endl;
        std::cerr << "  --to-wav                 Automatically decode and convert extracted ADX to WAV" << std::endl;
        std::cerr << "  --with-metadata          Extract with metadata.json for round-trip rebuilding" << std::endl;
        std::cerr << "  --encrypt-adx <in> <out> Encrypt an ADX file" << std::endl;
        return 1;
    }

    std::string firstArg = argv[1];

    if (firstArg == "--info") {
        if (argc < 3) {
            std::cerr << "Error: --info requires <input_file.acb|input_file.awb>." << std::endl;
            return 1;
        }
        return CriAudio::showFileInfo(argv[2]) ? 0 : 1;
    }

    if (firstArg == "--play") {
        if (argc < 3) {
            std::cerr << "Error: --play requires <input_file.acb|input_file.awb>." << std::endl;
            return 1;
        }
        int trackIndex = -1;  // -1 means interactive selection
        if (argc >= 4) {
            try {
                trackIndex = std::stoi(argv[3]);
            } catch (...) {
                std::cerr << "Error: invalid track index." << std::endl;
                return 1;
            }
        }
        return CriAudio::playAudio(argv[2], trackIndex) ? 0 : 1;
    }

    if (firstArg == "--decrypt-adx-file") {
        if (argc < 5) {
            std::cerr << "Error: --decrypt-adx-file requires <key_id> <input.adx> <output.adx>." << std::endl;
            return 1;
        }

        int keyId = 9;
        if (!parseKeyArg(argv[2], keyId)) {
            std::cerr << "Error: invalid key id. Expected 0..9." << std::endl;
            return 1;
        }

        return CriAudio::decryptAdxFile(argv[3], argv[4], keyId) ? 0 : 1;
    }

    if (firstArg == "--adx-to-wav") {
        if (argc < 4) {
            std::cerr << "Error: --adx-to-wav requires <input.adx> <output.wav>." << std::endl;
            return 1;
        }

        bool decryptFirst = false;
        int keyId = 9;
        double loopSeconds = 0.0;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--decrypt-key") {
                if (i + 1 >= argc || !parseKeyArg(argv[i + 1], keyId)) {
                    std::cerr << "Error: --decrypt-key requires key id in range 0..9." << std::endl;
                    return 1;
                }
                decryptFirst = true;
                ++i;
            } else if (arg == "--loop-seconds") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-seconds requires a positive number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopSeconds)) {
                    std::cerr << "Error: --loop-seconds must be a valid number." << std::endl;
                    return 1;
                }
                if (loopSeconds <= 0.0) {
                    std::cerr << "Error: --loop-seconds must be > 0." << std::endl;
                    return 1;
                }
            }
        }

        return CriAudio::adxFileToWav(argv[2], argv[3], decryptFirst, keyId, loopSeconds) ? 0 : 1;
    }

    if (firstArg == "--to-criware") {
        if (argc < 4) {
            std::cerr << "Error: --to-criware requires <input.(adx|wav|mp3)> <output.adx>." << std::endl;
            return 1;
        }

        int keyId = 9;
        bool encryptOutput = true;
        bool enableLoop = false;
        bool autoLoop = false;
        double loopStartSec = 0.0;
        double loopEndSec = -1.0;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--no-encrypt") {
                encryptOutput = false;
            } else if (arg == "--auto-loop") {
                autoLoop = true;
                enableLoop = true;
            } else if (arg == "--key") {
                if (i + 1 >= argc || !parseKeyArg(argv[i + 1], keyId)) {
                    std::cerr << "Error: --key requires key id in range 0..9." << std::endl;
                    return 1;
                }
                ++i;
            } else if (arg == "--loop-start-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-start-sec requires a non-negative number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopStartSec)) {
                    std::cerr << "Error: --loop-start-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopStartSec < 0.0) {
                    std::cerr << "Error: --loop-start-sec must be >= 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            } else if (arg == "--loop-end-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-end-sec requires a positive number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopEndSec)) {
                    std::cerr << "Error: --loop-end-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopEndSec <= 0.0) {
                    std::cerr << "Error: --loop-end-sec must be > 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            }
        }

        return CriAudio::convertToCriwareAdx(argv[2], argv[3], encryptOutput, keyId, enableLoop, loopStartSec, loopEndSec, autoLoop) ? 0 : 1;
    }

    if (firstArg == "--make-awb") {
        if (argc < 4) {
            std::cerr << "Error: --make-awb requires <output.awb> and at least one input." << std::endl;
            return 1;
        }

        std::string outputAwb = argv[2];
        int keyId = 9;
        bool encryptOutput = true;
        bool enableLoop = false;
        bool autoLoop = false;
        double loopStartSec = 0.0;
        double loopEndSec = -1.0;
        std::vector<std::string> inputs;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--no-encrypt") {
                encryptOutput = false;
            } else if (arg == "--auto-loop") {
                autoLoop = true;
                enableLoop = true;
            } else if (arg == "--key") {
                if (i + 1 >= argc || !parseKeyArg(argv[i + 1], keyId)) {
                    std::cerr << "Error: --key requires key id in range 0..9." << std::endl;
                    return 1;
                }
                ++i;
            } else if (arg == "--loop-start-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-start-sec requires a non-negative number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopStartSec)) {
                    std::cerr << "Error: --loop-start-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopStartSec < 0.0) {
                    std::cerr << "Error: --loop-start-sec must be >= 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            } else if (arg == "--loop-end-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-end-sec requires a positive number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopEndSec)) {
                    std::cerr << "Error: --loop-end-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopEndSec <= 0.0) {
                    std::cerr << "Error: --loop-end-sec must be > 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            } else {
                inputs.push_back(arg);
            }
        }

        if (inputs.empty()) {
            std::cerr << "Error: no inputs were provided." << std::endl;
            return 1;
        }

        return CriAudio::buildAwbFromInputs(inputs, outputAwb, encryptOutput, keyId, enableLoop, loopStartSec, loopEndSec, autoLoop) ? 0 : 1;
    }

    if (firstArg == "--make-acb-awb") {
        if (argc < 6) {
            std::cerr << "Error: --make-acb-awb requires <template.acb> <output.acb> <output.awb> <input...>." << std::endl;
            return 1;
        }

        std::string templateAcb = argv[2];
        std::string outputAcb = argv[3];
        std::string outputAwb = argv[4];
        int keyId = 9;
        bool encryptOutput = true;
        bool enableLoop = false;
        bool autoLoop = false;
        double loopStartSec = 0.0;
        double loopEndSec = -1.0;
        std::vector<std::string> inputs;

        for (int i = 5; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--no-encrypt") {
                encryptOutput = false;
            } else if (arg == "--auto-loop") {
                autoLoop = true;
                enableLoop = true;
            } else if (arg == "--key") {
                if (i + 1 >= argc || !parseKeyArg(argv[i + 1], keyId)) {
                    std::cerr << "Error: --key requires key id in range 0..9." << std::endl;
                    return 1;
                }
                ++i;
            } else if (arg == "--loop-start-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-start-sec requires a non-negative number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopStartSec)) {
                    std::cerr << "Error: --loop-start-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopStartSec < 0.0) {
                    std::cerr << "Error: --loop-start-sec must be >= 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            } else if (arg == "--loop-end-sec") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --loop-end-sec requires a positive number." << std::endl;
                    return 1;
                }
                if (!parseDoubleArg(argv[++i], loopEndSec)) {
                    std::cerr << "Error: --loop-end-sec must be a valid number." << std::endl;
                    return 1;
                }
                if (loopEndSec <= 0.0) {
                    std::cerr << "Error: --loop-end-sec must be > 0." << std::endl;
                    return 1;
                }
                enableLoop = true;
            } else {
                inputs.push_back(arg);
            }
        }

        if (inputs.empty()) {
            std::cerr << "Error: no inputs were provided." << std::endl;
            return 1;
        }

        return CriAudio::buildAcbAwbFromTemplate(templateAcb, inputs, outputAcb, outputAwb, encryptOutput, keyId, enableLoop, loopStartSec, loopEndSec, autoLoop) ? 0 : 1;
    }

    if (firstArg == "--encrypt-adx") {
        if (argc < 4) {
            std::cerr << "Error: --encrypt-adx requires input and output file paths." << std::endl;
            return 1;
        }

        int keyId = 9;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--key") {
                if (i + 1 >= argc || !parseKeyArg(argv[i + 1], keyId)) {
                    std::cerr << "Error: --key requires key id in range 0..9." << std::endl;
                    return 1;
                }
                ++i;
            }
        }

        std::string inputPath = argv[2];
        std::string outputPath = argv[3];
        if (CriAudio::encryptAdxFile(inputPath, outputPath, keyId)) {
            return 0;
        } else {
            return 1;
        }
    }

    std::string inputFile = argv[1];
    std::string outputDir = "";
    CriAudio::ExtractionOptions options;
    bool withMetadata = false;

    int argIndex = 2;
    if (argIndex < argc && argv[argIndex][0] != '-') {
        outputDir = argv[argIndex];
        argIndex++;
    } else {
        // Output directory based on input file name
        fs::path path(inputFile);
        outputDir = (path.parent_path() / (path.stem().string() + "_extracted")).string();
    }

    for (; argIndex < argc; ++argIndex) {
        std::string arg = argv[argIndex];
        if (arg == "--decrypt-adx") {
            if (argIndex + 1 < argc) {
                options.decryptAdx = true;
                options.adxKeyId = std::stoi(argv[++argIndex]);
            }
        } else if (arg == "--to-wav") {
            options.convertToWav = true;
        } else if (arg == "--with-metadata") {
            withMetadata = true;
        }
    }

    std::cout << "  Input     : " << inputFile << std::endl;
    std::cout << "  Output    : " << outputDir << std::endl;
    if (options.decryptAdx)  std::cout << "  Decrypt   : ADX key " << options.adxKeyId << std::endl;
    if (options.convertToWav) std::cout << "  Convert   : ADX -> WAV" << std::endl;
    if (withMetadata) std::cout << "  Metadata  : Enabled" << std::endl;
    std::cout << std::endl;

    if (!fs::exists(inputFile)) {
        std::cerr << "Error: input file does not exist: " << inputFile << std::endl;
        return 1;
    }

    bool success = false;
    if (withMetadata) {
        success = CriAudio::extractWithMetadata(inputFile, outputDir, options);
    } else {
        success = CriAudio::extractDependencies(inputFile, outputDir, options);
    }

    if (success) {
        std::cout << std::endl << "Extraction complete." << std::endl;
    } else {
        std::cerr << std::endl << "Extraction failed." << std::endl;
        return 1;
    }

    return 0;
}
