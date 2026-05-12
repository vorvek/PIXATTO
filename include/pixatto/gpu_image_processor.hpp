#pragma once

#include "pixatto/image.hpp"
#include "pixatto/image_processing.hpp"

#include <memory>
#include <string>

namespace pixatto {

class GpuImageProcessor {
public:
    GpuImageProcessor();
    ~GpuImageProcessor();

    GpuImageProcessor(const GpuImageProcessor&) = delete;
    GpuImageProcessor& operator=(const GpuImageProcessor&) = delete;

    [[nodiscard]] bool initialize(std::string& error);
    [[nodiscard]] bool process_sampled_collapsed(
        const Image& source,
        const ProcessSettings& settings,
        Image& result,
        std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool can_process_sampled_collapsed_on_gpu(const ProcessSettings& settings);

} // namespace pixatto
