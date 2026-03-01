#include <iostream>
#include <string>
#include <filesystem>
#include "extractor.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::cout << "CriAudioExtractor — CRI Middleware ACB/AWB demuxer" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file.acb|input_file.awb> [output_directory] [options...]" << std::endl;
        std::cerr << "\nOptions:" << std::endl;
        std::cerr << "  --decrypt-adx <key_id>   Decrypt ADX files using provided key id (e.g. 9)" << std::endl;
        std::cerr << "  --to-wav                 Automatically decode and convert extracted ADX to WAV" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputDir = "";
    CriAudio::ExtractionOptions options;

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
        }
    }

    std::cout << "  Input     : " << inputFile << std::endl;
    std::cout << "  Output    : " << outputDir << std::endl;
    if (options.decryptAdx)  std::cout << "  Decrypt   : ADX key " << options.adxKeyId << std::endl;
    if (options.convertToWav) std::cout << "  Convert   : ADX -> WAV" << std::endl;
    std::cout << std::endl;

    if (!fs::exists(inputFile)) {
        std::cerr << "Error: input file does not exist: " << inputFile << std::endl;
        return 1;
    }

    if (CriAudio::extractDependencies(inputFile, outputDir, options)) {
        std::cout << std::endl << "Extraction complete." << std::endl;
    } else {
        std::cerr << std::endl << "Extraction failed." << std::endl;
        return 1;
    }

    return 0;
}
