#include "gles/runtime.h"
#include <Windows.h>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace nova::gles {
namespace {
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("PNG encoder failed: HRESULT=" + std::to_string(hr));
}
} // namespace
void write_png(const std::filesystem::path &path, unsigned width, unsigned height,
               std::span<const uint8_t> rgba) {
    if (!width || !height || rgba.size() != static_cast<size_t>(width) * height * 4)
        throw std::invalid_argument("PNG dimensions do not match pixels");
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    check(initialized);
    struct Uninitialize {
        ~Uninitialize() { CoUninitialize(); }
    } cleanup;
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)));
    std::filesystem::create_directories(path.parent_path());
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream));
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("PNG encoder did not accept BGRA");
    std::vector<uint8_t> bgra(rgba.begin(), rgba.end());
    for (size_t i = 0; i < bgra.size(); i += 4)
        std::swap(bgra[i], bgra[i + 2]);
    check(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data()));
    check(frame->Commit());
    check(encoder->Commit());
}
} // namespace nova::gles
