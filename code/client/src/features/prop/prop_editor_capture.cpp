#include "prop_editor_capture.h"

#include <utils/safe_win32.h>

#include <DirectXPackedVector.h>
#include <graphics/backend/d3d12.h>
#include <openssl/evp.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace KCDC::Features::Prop {
    namespace {
        constexpr UINT kMaximumWidth           = 1600;
        constexpr UINT kMaximumHeight          = 900;
        constexpr UINT64 kMaximumReadbackBytes = 128ULL * 1024 * 1024;
        constexpr std::size_t kMaximumResults  = 4;
        constexpr auto kRequestTimeout         = std::chrono::seconds(10);

        bool Terminal(const nlohmann::json &result) {
            const auto &state = result.at("state");
            return state == "completed" || state == "failed" || state == "cancelled";
        }

        std::int64_t TimestampMilliseconds() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        class ScopedCom final {
          public:
            ScopedCom(): _result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
            ~ScopedCom() {
                if (SUCCEEDED(_result)) {
                    CoUninitialize();
                }
            }
            bool Usable() const {
                return SUCCEEDED(_result) || _result == RPC_E_CHANGED_MODE;
            }

          private:
            HRESULT _result;
        };

        std::string EncodePng(const std::vector<std::uint8_t> &pixels, UINT width, UINT height) {
            const ScopedCom com;
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            Microsoft::WRL::ComPtr<IStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            if (!com.Usable() || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
                throw std::runtime_error("Could not initialize the PNG encoder");
            }
            if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))
                || FAILED(frame->SetSize(width, height))) {
                throw std::runtime_error("Could not create the PNG frame");
            }
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
            if (FAILED(frame->SetPixelFormat(&format)) || FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE *>(pixels.data()), &bitmap))
                || FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
                throw std::runtime_error("Could not encode the PNG frame");
            }

            STATSTG stats {};
            LARGE_INTEGER beginning {};
            if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || stats.cbSize.QuadPart > 8ULL * 1024 * 1024 || FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr))) {
                throw std::runtime_error("PNG output exceeded its memory limit");
            }
            std::string png(static_cast<std::size_t>(stats.cbSize.QuadPart), '\0');
            ULONG read = 0;
            if (FAILED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) || read != png.size()) {
                throw std::runtime_error("Could not read the encoded PNG");
            }
            std::string encoded(4 * ((png.size() + 2) / 3) + 1, '\0');
            const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()), reinterpret_cast<const unsigned char *>(png.data()), static_cast<int>(png.size()));
            if (written < 0) {
                throw std::runtime_error("Could not encode the PNG response");
            }
            encoded.resize(static_cast<std::size_t>(written));
            return encoded;
        }

        std::array<float, 3> ReadPixel(const std::uint8_t *row, UINT x, DXGI_FORMAT format) {
            if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                std::uint16_t half[4];
                std::memcpy(half, row + static_cast<std::size_t>(x) * 8, sizeof(half));
                std::array<float, 3> rgb {};
                for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
                    const float value = DirectX::PackedVector::XMConvertHalfToFloat(half[channel]);
                    rgb[channel]      = std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
                }
                return rgb;
            }
            const auto *pixel = row + static_cast<std::size_t>(x) * 4;
            const bool bgra   = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
            return {pixel[bgra ? 2 : 0] / 255.0F, pixel[1] / 255.0F, pixel[bgra ? 0 : 2] / 255.0F};
        }

        std::vector<std::uint8_t> ConvertPixels(const std::uint8_t *source, UINT pitch, UINT sourceWidth, UINT sourceHeight, DXGI_FORMAT format, UINT width, UINT height) {
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
            for (UINT y = 0; y < height; ++y) {
                const float sy = std::clamp((static_cast<float>(y) + 0.5F) * sourceHeight / height - 0.5F, 0.0F, static_cast<float>(sourceHeight - 1));
                const UINT y0  = static_cast<UINT>(sy);
                const UINT y1  = std::min(y0 + 1, sourceHeight - 1);
                const float fy = sy - y0;
                for (UINT x = 0; x < width; ++x) {
                    const float sx         = std::clamp((static_cast<float>(x) + 0.5F) * sourceWidth / width - 0.5F, 0.0F, static_cast<float>(sourceWidth - 1));
                    const UINT x0          = static_cast<UINT>(sx);
                    const UINT x1          = std::min(x0 + 1, sourceWidth - 1);
                    const float fx         = sx - x0;
                    const auto topLeft     = ReadPixel(source + static_cast<std::size_t>(y0) * pitch, x0, format);
                    const auto topRight    = ReadPixel(source + static_cast<std::size_t>(y0) * pitch, x1, format);
                    const auto bottomLeft  = ReadPixel(source + static_cast<std::size_t>(y1) * pitch, x0, format);
                    const auto bottomRight = ReadPixel(source + static_cast<std::size_t>(y1) * pitch, x1, format);
                    std::array<float, 3> rgb {};
                    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
                        rgb[channel] = std::lerp(std::lerp(topLeft[channel], topRight[channel], fx), std::lerp(bottomLeft[channel], bottomRight[channel], fx), fy);
                    }
                    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                        // scRGB uses linear Rec.709 primaries, with 1.0 at 80 nits.
                        // Preserve hue with luminance Reinhard tone mapping before
                        // the sRGB transfer; the result is an SDR inspection image.
                        const float luminance = rgb[0] * 0.2126F + rgb[1] * 0.7152F + rgb[2] * 0.0722F;
                        for (float &channel : rgb) {
                            channel /= 1.0F + luminance;
                            channel = channel <= 0.0031308F ? 12.92F * channel : 1.055F * std::pow(channel, 1.0F / 2.4F) - 0.055F;
                        }
                    }
                    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
                    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
                        pixels[offset + channel] = static_cast<std::uint8_t>(std::lround(std::clamp(rgb[channel], 0.0F, 1.0F) * 255.0F));
                    }
                    pixels[offset + 3] = 255;
                }
            }
            return pixels;
        }
    } // namespace

    struct EditorCapture::State {
        struct Readback {
            std::mutex mutex;
            Microsoft::WRL::ComPtr<ID3D12Resource> source;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            Microsoft::WRL::ComPtr<ID3D12Fence> fence;
            HANDLE event = nullptr;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            UINT64 byteSize    = 0;
            std::uint64_t id   = 0;
            UINT width         = 0;
            UINT height        = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            bool submitted     = false;
            bool signalled     = false;

            ~Readback() {
                // Only a wedged live device can reach destruction with work
                // outstanding. At process shutdown retain those objects until
                // OS cleanup rather than freeing memory the GPU can still use.
                if (submitted && !Complete() && SUCCEEDED(device->GetDeviceRemovedReason())) {
                    source.Detach();
                    buffer.Detach();
                    allocator.Detach();
                    commands.Detach();
                    queue.Detach();
                    device.Detach();
                    fence.Detach();
                }
                else if (event) {
                    CloseHandle(event);
                }
            }

            bool Complete() const {
                return signalled && fence->GetCompletedValue() >= 1;
            }
            bool DeviceRemoved() const {
                return (signalled && fence->GetCompletedValue() == std::numeric_limits<UINT64>::max()) || FAILED(device->GetDeviceRemovedReason());
            }

            bool Wait(DWORD milliseconds) {
                if (Complete() || DeviceRemoved()) {
                    return true;
                }
                // A failed queue signal does not prove completion. Retry so
                // transient failures do not permanently pin a healthy device.
                if (!signalled) {
                    signalled = SUCCEEDED(queue->Signal(fence.Get(), 1));
                    if (signalled) {
                        fence->SetEventOnCompletion(1, event);
                    }
                }
                WaitForSingleObject(event, milliseconds);
                return Complete() || DeviceRemoved();
            }

            void ReleaseSource() {
                commands.Reset();
                allocator.Reset();
                source.Reset();
            }
        };

        mutable std::mutex mutex;
        std::mutex shutdownMutex;
        std::condition_variable wake;
        std::thread worker;
        bool stopping               = false;
        std::uint64_t nextId        = 0;
        std::uint64_t activeId      = 0;
        std::uint64_t frameSequence = 0;
        bool includeOverlays        = false;
        std::chrono::steady_clock::time_point requestedAt;
        std::deque<std::shared_ptr<nlohmann::json>> results;
        std::shared_ptr<Readback> readback;

        nlohmann::json *Find(std::uint64_t id) {
            for (auto &result : results) {
                if ((*result)["id"] == id) {
                    return result.get();
                }
            }
            return nullptr;
        }

        void Fail(std::uint64_t id, const std::string &reason) {
            if (auto *result = Find(id); result && !Terminal(*result)) {
                (*result)["state"] = "failed";
                (*result)["error"] = reason;
            }
        }
    };

    EditorCapture::EditorCapture(): _state(std::make_unique<State>()) {}
    EditorCapture::~EditorCapture() {
        Shutdown();
    }

    EditorCapture &EditorCapture::Get() {
        static EditorCapture capture;
        return capture;
    }

    std::uint64_t EditorCapture::Request(bool includeOverlays) {
        std::lock_guard lock(_state->mutex);
        if (_state->stopping || _state->activeId != 0) {
            return 0;
        }
        if (!_state->worker.joinable()) {
            _state->worker = std::thread(&EditorCapture::EncodeLoop, this);
        }
        while (_state->results.size() >= kMaximumResults) {
            _state->results.pop_front();
        }
        const std::uint64_t id = ++_state->nextId;
        _state->results.push_back(std::make_shared<nlohmann::json>(nlohmann::json {{"id", id}, {"state", "queued"}, {"includeOverlays", includeOverlays}}));
        _state->activeId        = id;
        _state->includeOverlays = includeOverlays;
        _state->requestedAt     = std::chrono::steady_clock::now();
        _state->wake.notify_all();
        return id;
    }

    nlohmann::json EditorCapture::GetResult(std::uint64_t id) const {
        std::shared_ptr<const nlohmann::json> completed;
        {
            std::lock_guard lock(_state->mutex);
            for (const auto &result : _state->results) {
                if ((*result)["id"] == id) {
                    if (!Terminal(*result)) {
                        return *result;
                    }
                    completed = result;
                    break;
                }
            }
        }
        // Terminal results are immutable. Copy their potentially large image
        // outside the lock used by the render hook.
        if (completed) {
            return *completed;
        }
        return {{"id", id}, {"state", "unknown"}, {"error", "Capture does not exist or its result expired"}};
    }

    bool EditorCapture::Cancel(std::uint64_t id) {
        std::lock_guard lock(_state->mutex);
        auto *result = _state->Find(id);
        if (!result || Terminal(*result)) {
            return false;
        }
        (*result)["state"] = "cancelled";
        if (_state->activeId == id && !_state->readback) {
            _state->activeId = 0;
        }
        _state->wake.notify_all();
        return true;
    }

    void EditorCapture::Poll() {
        std::lock_guard lock(_state->mutex);
        ++_state->frameSequence;
    }

    void EditorCapture::Record(Framework::Graphics::D3D12Backend &backend, bool overlaysRendered) {
        std::lock_guard lock(_state->mutex);
        if (_state->stopping || _state->activeId == 0 || _state->readback || _state->includeOverlays != overlaysRendered) {
            return;
        }
        const auto id = _state->activeId;
        try {
            auto readback    = std::make_shared<State::Readback>();
            readback->id     = id;
            readback->device = backend.GetDevice();
            readback->queue  = backend.GetCommandQueue();
            auto *chain      = backend.GetSwapChain();
            if (FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&readback->source)))) {
                throw std::runtime_error("Could not acquire the rendered back buffer");
            }
            const auto description = readback->source->GetDesc();
            const auto format      = description.Format;
            const bool supported   = format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM
                                   || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!supported || description.SampleDesc.Count != 1 || description.Width == 0 || description.Width > 16384 || description.Height == 0 || description.Height > 16384) {
                throw std::runtime_error("Unsupported back-buffer format, sample count or dimensions");
            }
            readback->width  = static_cast<UINT>(description.Width);
            readback->height = description.Height;
            readback->format = format;
            readback->device->GetCopyableFootprints(&description, 0, 1, 0, &readback->footprint, nullptr, nullptr, &readback->byteSize);
            if (readback->byteSize == 0 || readback->byteSize > kMaximumReadbackBytes) {
                throw std::runtime_error("Back-buffer readback exceeds 128 MiB");
            }
            D3D12_HEAP_PROPERTIES heap {};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer {};
            buffer.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width            = readback->byteSize;
            buffer.Height           = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels        = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(readback->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback->buffer)))
                || FAILED(readback->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&readback->allocator)))
                || FAILED(readback->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, readback->allocator.Get(), nullptr, IID_PPV_ARGS(&readback->commands))) || FAILED(readback->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&readback->fence)))) {
                throw std::runtime_error("Could not allocate GPU readback resources");
            }
            readback->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!readback->event) {
                throw std::runtime_error("Could not create the GPU completion event");
            }
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = readback->source.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            readback->commands->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source {};
            source.pResource = readback->source.Get();
            source.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination {};
            destination.pResource       = readback->buffer.Get();
            destination.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = readback->footprint;
            readback->commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            readback->commands->ResourceBarrier(1, &barrier);
            if (FAILED(readback->commands->Close())) {
                throw std::runtime_error("Could not close the GPU capture command list");
            }
            auto *result                  = _state->Find(id);
            (*result)["state"]            = "capturing";
            (*result)["frameSequence"]    = _state->frameSequence;
            (*result)["capturedAtUnixMs"] = TimestampMilliseconds();
            (*result)["sourceWidth"]      = readback->width;
            (*result)["sourceHeight"]     = readback->height;
            (*result)["sourceFormat"]     = static_cast<unsigned int>(format);
            (*result)["colorConversion"]  = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "scRGB to SDR: luminance Reinhard, sRGB transfer" : "display-encoded sRGB";
            (*result)["metadataScope"]    = "Rendered back buffer before Present; no editor-revision or camera synchronization guarantee";
            _state->readback              = readback;
            ID3D12CommandList *commands[] = {readback->commands.Get()};
            readback->queue->ExecuteCommandLists(1, commands);
            readback->submitted = true;
            readback->signalled = SUCCEEDED(readback->queue->Signal(readback->fence.Get(), 1));
            if (readback->signalled) {
                readback->fence->SetEventOnCompletion(1, readback->event);
            }
            _state->wake.notify_all();
        }
        catch (const std::exception &error) {
            _state->Fail(id, error.what());
            _state->activeId = 0;
        }
    }

    bool EditorCapture::PrepareResize() {
        std::shared_ptr<State::Readback> readback;
        {
            std::lock_guard lock(_state->mutex);
            readback = _state->readback;
        }
        if (!readback) {
            return true;
        }
        std::lock_guard lock(readback->mutex);
        if (!readback->Wait(500)) {
            return false;
        }
        readback->ReleaseSource();
        return true;
    }

    void EditorCapture::EncodeLoop() {
        for (;;) {
            std::shared_ptr<State::Readback> readback;
            {
                std::unique_lock lock(_state->mutex);
                _state->wake.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return _state->stopping || _state->readback != nullptr;
                });
                if (_state->stopping && !_state->readback) {
                    return;
                }
                readback = _state->readback;
                if (!readback) {
                    if (_state->activeId && std::chrono::steady_clock::now() - _state->requestedAt > kRequestTimeout) {
                        _state->Fail(_state->activeId, "Timed out waiting for a rendered frame");
                        _state->activeId = 0;
                    }
                    continue;
                }
            }
            bool stopping  = false;
            bool cancelled = false;
            {
                std::lock_guard lock(_state->mutex);
                stopping           = _state->stopping;
                const auto *result = _state->Find(readback->id);
                cancelled          = !result || Terminal(*result);
                if (std::chrono::steady_clock::now() - _state->requestedAt > kRequestTimeout) {
                    _state->Fail(readback->id, "Timed out waiting for GPU readback");
                    cancelled = true;
                }
            }
            bool complete      = false;
            bool deviceRemoved = false;
            {
                std::lock_guard lock(readback->mutex);
                complete      = readback->Wait(stopping ? 500 : 50);
                deviceRemoved = readback->DeviceRemoved();
                if (complete) {
                    readback->ReleaseSource();
                }
            }
            if (!complete && !stopping) {
                continue;
            }
            if (!stopping && !cancelled && !deviceRemoved) {
                try {
                    const double scale = std::min({1.0, static_cast<double>(kMaximumWidth) / readback->width, static_cast<double>(kMaximumHeight) / readback->height});
                    const UINT width   = std::max(1U, static_cast<UINT>(std::lround(readback->width * scale)));
                    const UINT height  = std::max(1U, static_cast<UINT>(std::lround(readback->height * scale)));
                    void *mapped       = nullptr;
                    const D3D12_RANGE range {0, static_cast<SIZE_T>(readback->byteSize)};
                    if (FAILED(readback->buffer->Map(0, &range, &mapped))) {
                        throw std::runtime_error("Could not map the completed GPU readback");
                    }
                    std::vector<std::uint8_t> pixels;
                    try {
                        pixels = ConvertPixels(static_cast<const std::uint8_t *>(mapped) + readback->footprint.Offset, readback->footprint.Footprint.RowPitch, readback->width, readback->height, readback->format, width, height);
                    }
                    catch (...) {
                        const D3D12_RANGE noWrite {0, 0};
                        readback->buffer->Unmap(0, &noWrite);
                        throw;
                    }
                    const D3D12_RANGE noWrite {0, 0};
                    readback->buffer->Unmap(0, &noWrite);
                    {
                        std::lock_guard lock(_state->mutex);
                        if (auto *result = _state->Find(readback->id); result && !Terminal(*result)) {
                            (*result)["state"] = "encoding";
                        }
                    }
                    auto image = EncodePng(pixels, width, height);
                    std::lock_guard lock(_state->mutex);
                    if (auto *result = _state->Find(readback->id); result && !Terminal(*result) && !_state->stopping) {
                        (*result)["state"]    = "completed";
                        (*result)["mimeType"] = "image/png";
                        (*result)["data"]     = std::move(image);
                        (*result)["width"]    = width;
                        (*result)["height"]   = height;
                    }
                }
                catch (const std::exception &error) {
                    std::lock_guard lock(_state->mutex);
                    _state->Fail(readback->id, error.what());
                }
            }
            {
                std::lock_guard lock(_state->mutex);
                if (deviceRemoved) {
                    _state->Fail(readback->id, "Graphics device was removed during capture");
                }
                _state->readback.reset();
                _state->activeId = 0;
            }
            if (stopping) {
                return;
            }
        }
    }

    void EditorCapture::Shutdown() {
        std::lock_guard shutdownLock(_state->shutdownMutex);
        {
            std::lock_guard lock(_state->mutex);
            _state->stopping = true;
            if (_state->activeId) {
                if (auto *result = _state->Find(_state->activeId); result && !Terminal(*result)) {
                    (*result)["state"] = "cancelled";
                }
            }
            _state->wake.notify_all();
        }
        if (_state->worker.joinable()) {
            _state->worker.join();
        }
    }
} // namespace KCDC::Features::Prop
