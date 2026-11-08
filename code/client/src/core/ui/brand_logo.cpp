#include "brand_logo.h"

#include <utils/safe_win32.h>

#include <imgui_internal.h>
#include <logging/logger.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include "core/ui/native_ui/native_ui_assets.h"

namespace KCDC::Core {
    namespace {
        // The logo's height as a fraction of the screen's: 43 px at 1080p,
        // 86 px at 2160p. The width follows the art's aspect, capped so a
        // portrait or 4:3 window does not run it into the middle.
        constexpr float kHeight   = 0.04F;
        constexpr float kMaxWidth = 0.25F;

        // The gap to the left and bottom edges, also against the height, so
        // it keeps its proportion to the logo on any aspect.
        constexpr float kMargin = 0.02F;

        /** COM for the WIC calls, balanced only when this call was the one that initialised it. */
        class ScopedCom final {
          public:
            ScopedCom(): _result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
            ~ScopedCom() {
                if (SUCCEEDED(_result)) {
                    CoUninitialize();
                }
            }

            /** RPC_E_CHANGED_MODE means the thread already has an apartment, which serves WIC just as well. */
            bool IsUsable() const {
                return SUCCEEDED(_result) || _result == RPC_E_CHANGED_MODE;
            }

          private:
            HRESULT _result;
        };

        Microsoft::WRL::ComPtr<IWICImagingFactory> CreateFactory() {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
                return nullptr;
            }
            return factory;
        }
    } // namespace

    void BrandLogo::Render() {
        CollectRetired();

        if (_loadFailed || (_source.empty() && !Load())) {
            return;
        }

        // Sized in back-buffer pixels, which is what the texture has to
        // match; laid out in the window space ImGui draws in. The two differ
        // whenever the game renders at a resolution its window does not carry.
        const ImGuiIO &io   = ImGui::GetIO();
        const ImVec2 scale  = io.DisplayFramebufferScale;
        const float screenW = io.DisplaySize.x * scale.x;
        const float screenH = io.DisplaySize.y * scale.y;
        if (screenW <= 0.0F || screenH <= 0.0F) {
            return;
        }

        const float aspect = static_cast<float>(_sourceWidth) / static_cast<float>(_sourceHeight);
        const float width  = std::min(screenH * kHeight * aspect, screenW * kMaxWidth);
        const int pixelsW  = static_cast<int>(std::lround(width));
        const int pixelsH  = static_cast<int>(std::lround(width / aspect));
        if (pixelsW <= 0 || pixelsH <= 0) {
            return;
        }

        if ((_texture == nullptr || _texture->Width != pixelsW || _texture->Height != pixelsH) && !Rebuild(pixelsW, pixelsH)) {
            return;
        }

        // Whole pixels, so every texel lands on exactly one of them.
        const float margin = std::round(screenH * kMargin);
        const ImVec2 min(margin / scale.x, (screenH - margin - static_cast<float>(pixelsH)) / scale.y);
        const ImVec2 max(min.x + static_cast<float>(pixelsW) / scale.x, min.y + static_cast<float>(pixelsH) / scale.y);
        ImGui::GetBackgroundDrawList()->AddImage(_texture->GetTexRef(), min, max);
    }

    bool BrandLogo::Load() {
        const auto log         = Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        const std::string path = NativeUIAssets::ModuleDirectory() + "\\files\\kcdc\\logo.png";
        _loadFailed            = true;

        const ScopedCom com;
        const Microsoft::WRL::ComPtr<IWICImagingFactory> factory = com.IsUsable() ? CreateFactory() : nullptr;
        if (factory == nullptr) {
            log->error("Logo: WIC is unavailable, not drawing it");
            return false;
        }

        // The directory comes back in the ANSI code page, which is also
        // what std::filesystem::path converts from.
        const std::filesystem::path widePath(path);
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame))) {
            log->error("Logo: {} is missing or unreadable", path);
            return false;
        }

        // Premultiplied, so the resampler averages colour weighted by
        // coverage and the transparent black around the art does not bleed
        // into its edges.
        UINT width  = 0;
        UINT height = 0;
        if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) || FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
            log->error("Logo: {} could not be converted to RGBA", path);
            return false;
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
            log->error("Logo: {} could not be decoded", path);
            return false;
        }

        _source       = std::move(pixels);
        _sourceWidth  = static_cast<int>(width);
        _sourceHeight = static_cast<int>(height);
        _loadFailed   = false;
        return true;
    }

    bool BrandLogo::Rebuild(int width, int height) {
        const ScopedCom com;
        const Microsoft::WRL::ComPtr<IWICImagingFactory> factory = com.IsUsable() ? CreateFactory() : nullptr;
        Microsoft::WRL::ComPtr<IWICBitmap> source;
        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        const UINT sourceStride = static_cast<UINT>(_sourceWidth) * 4;

        // The scaler hands back whatever format it resampled in -- PBGRA in
        // practice, which drew the logo with red and blue swapped -- so the
        // result is converted back to the byte order the texture takes.
        if (factory == nullptr || FAILED(factory->CreateBitmapFromMemory(_sourceWidth, _sourceHeight, GUID_WICPixelFormat32bppPRGBA, sourceStride, static_cast<UINT>(_source.size()), _source.data(), &source)) || FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(source.Get(), width, height, WICBitmapInterpolationModeFant)) || FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Logo: could not resample to {}x{}", width, height);
            _loadFailed = true;
            return false;
        }

        ImTextureData *texture = IM_NEW(ImTextureData)();
        texture->Create(ImTextureFormat_RGBA32, width, height);
        if (FAILED(converter->CopyPixels(nullptr, texture->GetPitch(), static_cast<UINT>(texture->GetSizeInBytes()), texture->Pixels))) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Logo: could not resample to {}x{}", width, height);
            IM_DELETE(texture);
            _loadFailed = true;
            return false;
        }

        // ImGui blends straight alpha; undo the premultiplication the
        // resampler needed.
        std::uint8_t *pixel = texture->Pixels;
        for (int i = 0; i < width * height; ++i, pixel += 4) {
            const unsigned alpha = pixel[3];
            if (alpha != 0 && alpha != 255) {
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<std::uint8_t>(std::min(255U, (pixel[channel] * 255U + alpha / 2) / alpha));
                }
            }
        }

        texture->UseColors = true;
        texture->UsedRect  = {0, 0, static_cast<unsigned short>(width), static_cast<unsigned short>(height)};
        ImGui::RegisterUserTexture(texture);

        if (_texture != nullptr) {
            // Still in this frame's draw data until the next NewFrame; the
            // flag turns into WantDestroy there, and the backend waits out
            // the frames in flight before it lets go.
            _texture->WantDestroyNextFrame = true;
            _retired.push_back(_texture);
        }
        _texture = texture;
        return true;
    }

    void BrandLogo::CollectRetired() {
        std::erase_if(_retired, [](ImTextureData *texture) {
            if (texture->Status != ImTextureStatus_Destroyed) {
                return false;
            }
            ImGui::UnregisterUserTexture(texture);
            IM_DELETE(texture);
            return true;
        });
    }
} // namespace KCDC::Core
