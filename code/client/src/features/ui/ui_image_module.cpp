#include <utils/safe_win32.h>

#include "ui_image_module.h"

#include "sdk/cryengine/scaleform/i_scaleform_helper.h"
#include "sdk/cryengine/system/s_system_global_environment.h"
#include "shared/image/png_image.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>

namespace KCDC::Features::UI {
    namespace {
        /** One answer to one image request; the engine releases it once (see IFlashLoadMovieImage). */
        class MemoryImage final: public SDK::IFlashLoadMovieImage {
          public:
            explicit MemoryImage(std::shared_ptr<const UiImageModule::Pixels> pixels): _pixels(std::move(pixels)) {}
            void Release() override {
                delete this;
            }
            int GetWidth() const override {
                return _pixels->width;
            }
            int GetHeight() const override {
                return _pixels->height;
            }
            int GetPitch() const override {
                return _pixels->width * 4;
            }
            void *GetPtr() const override {
                return const_cast<std::uint8_t *>(_pixels->rgba.data());
            }
            // In game, ARGB_8888 reads the bytes as R, G, B, A (BGRA showed red and blue swapped).
            EFmt GetFormat() const override {
                return EFmt::ARGB_8888;
            }

          private:
            std::shared_ptr<const UiImageModule::Pixels> _pixels;
        };
    } // namespace

    UiImageModule &UiImageModule::Get() {
        static UiImageModule instance;
        return instance;
    }

    std::shared_ptr<const UiImageModule::Pixels> UiImageModule::Decode(const std::string &png, std::uint32_t side) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto pixels       = std::make_shared<Pixels>();
        bool ok           = false;
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            Microsoft::WRL::ComPtr<IWICStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> premultiplied;
            Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
            Microsoft::WRL::ComPtr<IWICFormatConverter> straight;
            UINT width  = 0;
            UINT height = 0;
            ok          = (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->CreateStream(&stream))
                 && SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(png.data())), static_cast<DWORD>(png.size()))) && SUCCEEDED(factory->CreateDecoder(GUID_ContainerFormatPng, nullptr, &decoder))
                 && SUCCEEDED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height));
            const KCDC::Shared::Image::ImageFit fit = KCDC::Shared::Image::FitImage(width, height, side);
            ok                                      = ok && SUCCEEDED(factory->CreateFormatConverter(&premultiplied)) && SUCCEEDED(premultiplied->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
                 && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(premultiplied.Get(), fit.width, fit.height, WICBitmapInterpolationModeHighQualityCubic)) && SUCCEEDED(factory->CreateFormatConverter(&straight))
                 && SUCCEEDED(straight->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
            std::vector<std::uint8_t> scaled(static_cast<std::size_t>(fit.width) * fit.height * 4);
            ok = ok && SUCCEEDED(straight->CopyPixels(nullptr, fit.width * 4, static_cast<UINT>(scaled.size()), scaled.data()));
            if (ok) {
                pixels->width  = static_cast<int>(side);
                pixels->height = static_cast<int>(side);
                pixels->rgba   = KCDC::Shared::Image::CenterImage(scaled, fit, side);
            }
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return ok ? pixels : nullptr;
    }

    std::string UiImageModule::Key(std::string_view path) {
        std::string key(path);
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return key;
    }

    void UiImageModule::Serve(std::string_view path, std::shared_ptr<const Pixels> pixels) {
        // The engine has one handler slot and nothing in WHGame.dll fills it.
        if (!_installed && SDK::SSystemGlobalEnvironment::Get().pScaleformHelper != nullptr) {
            SDK::SSystemGlobalEnvironment::Get().pScaleformHelper->SetFlashLoadMovieHandler(this);
            _installed = true;
        }
        std::scoped_lock lock(_mutex);
        _images.insert_or_assign(Key(path), std::move(pixels));
    }

    void UiImageModule::Withdraw(std::string_view path) {
        std::scoped_lock lock(_mutex);
        if (const auto found = _images.find(Key(path)); found != _images.end()) {
            _images.erase(found);
        }
    }

    bool UiImageModule::Serves(std::string_view path) const {
        std::scoped_lock lock(_mutex);
        return _images.contains(Key(path));
    }

    SDK::IFlashLoadMovieImage *UiImageModule::LoadMovie(const char *filePath) {
        if (filePath == nullptr) {
            return nullptr;
        }
        std::shared_ptr<const Pixels> pixels;
        {
            std::scoped_lock lock(_mutex);
            const auto found = _images.find(Key(filePath));
            if (found == _images.end()) {
                return nullptr;
            }
            pixels = found->second;
        }
        return new MemoryImage(std::move(pixels));
    }
} // namespace KCDC::Features::UI
