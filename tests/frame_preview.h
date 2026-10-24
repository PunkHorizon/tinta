#pragma once
#include "app.h"
#include "editor.h"
#include "tabs.h"
#include <d2d1_1.h>
#include <filesystem>
#include <wrl/client.h>

// One frame as drawn, the tab strip and the editor or the reading view's
// retained layout, with `overlay` on top, saved as a PNG for inspection.
// The window target's device context draws into a bitmap for it, which is
// copied to the CPU.
template <typename Overlay>
bool saveFramePreview(App& app, const std::filesystem::path& path, Overlay overlay) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1DeviceContext> dc;
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) return false;
    const UINT width = (UINT)app.width, height = (UINT)app.height;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1Bitmap1> target, readable;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                    format), &readable))) {
        return false;
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    if (app.editMode && !app.editorReadingPreview) {
        dc->Clear(editDeskColor(app));
        renderEditor(app, editorPaneWidth(app));
    } else {
        dc->Clear(app.theme.background);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app) - app.scrollX, -app.scrollY));
        for (const auto& item : app.layoutRects) {
            app.brush->SetColor(item.color);
            dc->FillRectangle(item.rect, app.brush);
        }
        for (const auto& item : app.layoutLines) {
            app.brush->SetColor(item.color);
            dc->DrawLine(item.p1, item.p2, app.brush, item.stroke);
        }
        for (const auto& item : app.layoutTextRuns) {
            app.brush->SetColor(item.color);
            dc->DrawTextLayout(item.pos, item.layout, app.brush);
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    renderTabStrip(app);
    overlay();
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return false;
    }
    ComPtr<IWICBitmap> image;
    const bool copied = SUCCEEDED(app.wicFactory->CreateBitmapFromMemory(
        width, height, GUID_WICPixelFormat32bppPBGRA, mapped.pitch, mapped.pitch * height, mapped.bits, &image));
    readable->Unmap();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    return copied && SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->WriteSource(image.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(encoder->Commit());
}

inline bool saveFramePreview(App& app, const std::filesystem::path& path) {
    return saveFramePreview(app, path, [] {});
}
