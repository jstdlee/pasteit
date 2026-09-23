#pragma once

#include "annotation/image_annotation.hpp"

#include <filesystem>
#include <string>

namespace pastit {

struct AnnotationExportResult {
    bool success = false;
    std::filesystem::path output_path;
    std::string format;
    std::string error;
};

AnnotationExportResult export_annotation_svg(const AnnotationDocument& document,
                                             const std::filesystem::path& output_path);

}  // namespace pastit
