#pragma once

#include "pixatto/image.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace pixatto {

class VideoPlaybackDecoder {
public:
    VideoPlaybackDecoder();
    ~VideoPlaybackDecoder();

    VideoPlaybackDecoder(const VideoPlaybackDecoder&) = delete;
    VideoPlaybackDecoder& operator=(const VideoPlaybackDecoder&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& error);
    void close();
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    [[nodiscard]] bool seek(double seconds, std::string& error);
    [[nodiscard]] bool read_next_frame(Image& frame, double& timestamp_seconds, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pixatto
