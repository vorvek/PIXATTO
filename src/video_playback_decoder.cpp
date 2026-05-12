#include "pixatto/video_playback_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>
#endif

namespace pixatto {
namespace {

#if defined(_WIN32)
template <typename T>
void release_com(T*& value)
{
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::string hresult_error(const char* action, HRESULT hr)
{
    std::ostringstream output;
    output << action << " failed with HRESULT 0x" << std::hex << std::uppercase
           << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(hr) << ".";
    return output.str();
}

bool succeeded_or_set_error(HRESULT hr, const char* action, std::string& error)
{
    if (SUCCEEDED(hr)) {
        return true;
    }
    error = hresult_error(action, hr);
    return false;
}
#endif

} // namespace

struct VideoPlaybackDecoder::Impl {
#if defined(_WIN32)
    IMFSourceReader* reader = nullptr;
    bool com_initialized = false;
    bool media_foundation_started = false;
    int frame_width = 0;
    int frame_height = 0;
    long stride = 0;

    ~Impl()
    {
        close();
        if (media_foundation_started) {
            MFShutdown();
            media_foundation_started = false;
        }
        if (com_initialized) {
            CoUninitialize();
            com_initialized = false;
        }
    }

    bool ensure_runtime(std::string& error)
    {
        if (!com_initialized) {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(com)) {
                com_initialized = true;
            } else if (com != RPC_E_CHANGED_MODE) {
                return succeeded_or_set_error(com, "CoInitializeEx", error);
            }
        }

        if (!media_foundation_started) {
            const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
            if (!succeeded_or_set_error(hr, "MFStartup", error)) {
                return false;
            }
            media_foundation_started = true;
        }
        return true;
    }

    bool update_media_type(std::string& error)
    {
        IMFMediaType* current_type = nullptr;
        HRESULT hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current_type);
        if (!succeeded_or_set_error(hr, "IMFSourceReader::GetCurrentMediaType", error)) {
            return false;
        }

        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(current_type, MF_MT_FRAME_SIZE, &width, &height);
        if (!succeeded_or_set_error(hr, "MFGetAttributeSize(MF_MT_FRAME_SIZE)", error)) {
            release_com(current_type);
            return false;
        }

        UINT32 stride_value = 0;
        hr = current_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_value);
        if (FAILED(hr)) {
            LONG computed_stride = 0;
            hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &computed_stride);
            if (!succeeded_or_set_error(hr, "MFGetStrideForBitmapInfoHeader", error)) {
                release_com(current_type);
                return false;
            }
            stride = computed_stride;
        } else {
            stride = static_cast<long>(stride_value);
        }

        frame_width = static_cast<int>(width);
        frame_height = static_cast<int>(height);
        release_com(current_type);
        return frame_width > 0 && frame_height > 0 && stride != 0;
    }

    bool open(const std::filesystem::path& path, std::string& error)
    {
        close();
        if (!ensure_runtime(error)) {
            return false;
        }

        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (!succeeded_or_set_error(hr, "MFCreateAttributes", error)) {
            return false;
        }
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        const std::wstring url = std::filesystem::absolute(path).wstring();
        hr = MFCreateSourceReaderFromURL(url.c_str(), attributes, &reader);
        release_com(attributes);
        if (!succeeded_or_set_error(hr, "MFCreateSourceReaderFromURL", error)) {
            close();
            return false;
        }

        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (!succeeded_or_set_error(hr, "IMFSourceReader::SetStreamSelection", error)) {
            close();
            return false;
        }

        IMFMediaType* media_type = nullptr;
        hr = MFCreateMediaType(&media_type);
        if (!succeeded_or_set_error(hr, "MFCreateMediaType", error)) {
            close();
            return false;
        }
        media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, media_type);
        release_com(media_type);
        if (!succeeded_or_set_error(hr, "IMFSourceReader::SetCurrentMediaType", error)) {
            close();
            return false;
        }

        if (!update_media_type(error)) {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        release_com(reader);
        frame_width = 0;
        frame_height = 0;
        stride = 0;
    }

    bool seek(double seconds, std::string& error)
    {
        if (!reader) {
            error = "Playback decoder is not open.";
            return false;
        }

        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = static_cast<LONGLONG>(std::max(0.0, seconds) * 10000000.0);
        const HRESULT hr = reader->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        return succeeded_or_set_error(hr, "IMFSourceReader::SetCurrentPosition", error);
    }

    bool read_next_frame(Image& frame, double& timestamp_seconds, std::string& error)
    {
        frame = {};
        timestamp_seconds = 0.0;
        if (!reader) {
            error = "Playback decoder is not open.";
            return false;
        }

        for (int attempt = 0; attempt < 32; ++attempt) {
            DWORD stream_index = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                &stream_index,
                &flags,
                &timestamp,
                &sample);
            if (!succeeded_or_set_error(hr, "IMFSourceReader::ReadSample", error)) {
                release_com(sample);
                return false;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0U) {
                release_com(sample);
                return false;
            }
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0U) {
                release_com(sample);
                if (!update_media_type(error)) {
                    return false;
                }
                continue;
            }
            if (!sample) {
                continue;
            }

            IMFMediaBuffer* buffer = nullptr;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            release_com(sample);
            if (!succeeded_or_set_error(hr, "IMFSample::ConvertToContiguousBuffer", error)) {
                return false;
            }

            BYTE* data = nullptr;
            DWORD max_length = 0;
            DWORD current_length = 0;
            hr = buffer->Lock(&data, &max_length, &current_length);
            if (!succeeded_or_set_error(hr, "IMFMediaBuffer::Lock", error)) {
                release_com(buffer);
                return false;
            }

            const int abs_stride = std::abs(stride);
            const std::size_t minimum_size = static_cast<std::size_t>(abs_stride) * static_cast<std::size_t>(frame_height);
            if (!data || current_length < minimum_size) {
                buffer->Unlock();
                release_com(buffer);
                error = "Media Foundation returned an incomplete video frame.";
                return false;
            }

            frame.width = frame_width;
            frame.height = frame_height;
            frame.rgba.resize(static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 4U);
            for (int y = 0; y < frame_height; ++y) {
                const int source_y = stride < 0 ? frame_height - 1 - y : y;
                const BYTE* source = data + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(abs_stride);
                unsigned char* destination = frame.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_width) * 4U;
                for (int x = 0; x < frame_width; ++x) {
                    destination[x * 4 + 0] = source[x * 4 + 2];
                    destination[x * 4 + 1] = source[x * 4 + 1];
                    destination[x * 4 + 2] = source[x * 4 + 0];
                    destination[x * 4 + 3] = 255;
                }
            }

            buffer->Unlock();
            release_com(buffer);
            timestamp_seconds = static_cast<double>(timestamp) / 10000000.0;
            return true;
        }

        error = "Media Foundation did not return a decoded video frame.";
        return false;
    }
#else
    int frame_width = 0;
    int frame_height = 0;

    bool open(const std::filesystem::path&, std::string& error)
    {
        error = "Native video playback is not implemented on this platform yet.";
        return false;
    }

    void close()
    {
        frame_width = 0;
        frame_height = 0;
    }

    bool seek(double, std::string& error)
    {
        error = "Native video playback is not implemented on this platform yet.";
        return false;
    }

    bool read_next_frame(Image&, double&, std::string& error)
    {
        error = "Native video playback is not implemented on this platform yet.";
        return false;
    }
#endif
};

VideoPlaybackDecoder::VideoPlaybackDecoder()
    : impl_(std::make_unique<Impl>())
{
}

VideoPlaybackDecoder::~VideoPlaybackDecoder() = default;

bool VideoPlaybackDecoder::open(const std::filesystem::path& path, std::string& error)
{
    return impl_->open(path, error);
}

void VideoPlaybackDecoder::close()
{
    impl_->close();
}

bool VideoPlaybackDecoder::is_open() const noexcept
{
#if defined(_WIN32)
    return impl_ && impl_->reader != nullptr;
#else
    return false;
#endif
}

int VideoPlaybackDecoder::width() const noexcept
{
    return impl_ ? impl_->frame_width : 0;
}

int VideoPlaybackDecoder::height() const noexcept
{
    return impl_ ? impl_->frame_height : 0;
}

bool VideoPlaybackDecoder::seek(double seconds, std::string& error)
{
    return impl_->seek(seconds, error);
}

bool VideoPlaybackDecoder::read_next_frame(Image& frame, double& timestamp_seconds, std::string& error)
{
    return impl_->read_next_frame(frame, timestamp_seconds, error);
}

} // namespace pixatto
