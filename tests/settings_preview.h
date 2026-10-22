#pragma once
#include "app.h"
#include "overlays.h"
#include <filesystem>

// Render the real settings into a GDI-compatible D2D target so its pixels
// can be inspected without displaying or capturing the user's desktop.
inline bool saveSettingsPreview(App& app, const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1HwndRenderTarget> target;
    auto props = D2D1::RenderTargetProperties();
    props.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
    if (FAILED(app.d2dFactory->CreateHwndRenderTarget(props,
        D2D1::HwndRenderTargetProperties(app.hwnd, D2D1::SizeU(app.width, app.height)), &target))) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush))) return false;
    auto previousTarget = app.renderTarget;
    auto previousBrush = app.brush;
    app.renderTarget = target.Get();
    app.brush = brush.Get();
    target->BeginDraw();
    target->Clear(app.theme.background);
    renderSettingsOverlay(app);
    ComPtr<ID2D1GdiInteropRenderTarget> interop;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    if (SUCCEEDED(target.As(&interop)) && SUCCEEDED(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc))) {
        HDC copy = CreateCompatibleDC(dc);
        bitmap = CreateCompatibleBitmap(dc, app.width, app.height);
        auto old = SelectObject(copy, bitmap);
        BitBlt(copy, 0, 0, app.width, app.height, dc, 0, 0, SRCCOPY);
        SelectObject(copy, old);
        DeleteDC(copy);
        interop->ReleaseDC(nullptr);
    }
    const auto drawn = target->EndDraw();
    app.renderTarget = previousTarget;
    app.brush = previousBrush;
    if (!bitmap) return false;
    ComPtr<IWICBitmap> image;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    const bool ok = SUCCEEDED(drawn) &&
        SUCCEEDED(app.wicFactory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &image)) &&
        SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->WriteSource(image.Get(), nullptr)) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    DeleteObject(bitmap);
    return ok;
}
