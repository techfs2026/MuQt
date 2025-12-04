#include "infersession.h"
#include <filesystem>

namespace RapidOCR {

void InferSession::verifyModel(const std::string& modelPath) {
    if (modelPath.empty()) {
        throw std::invalid_argument("model_path is empty!");
    }

    std::filesystem::path path(modelPath);

    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(modelPath + " does not exist.");
    }

    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error(modelPath + " is not a file.");
    }
}

} // namespace RapidOCR
