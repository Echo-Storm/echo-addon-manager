#pragma once
// Offscreen ImGui renderer for looking at the UI without opening a window or touching anyone's screen.
// Creates a D3D11 device (hardware, falling back to WARP), an offscreen render target, drives a few ImGui frames
// through the stock DX11 backend and writes the last one to a BMP. Header only; the caller compiles imgui and
// backends/imgui_impl_dx11.cpp.
//
//   ImGui::CreateContext();  ...load fonts / SetupModernStyle...
//   UiShot shot; shot.Init(900, 700);
//   shot.Frame([&]{ /* ImGui calls */ }, /*frames*/ 3);
//   shot.Save("out.bmp");
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#pragma comment(lib, "d3d11.lib")

struct UiShot {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    int w = 0, h = 0;
    ImVec4 clear = ImVec4(0.094f, 0.094f, 0.094f, 1.0f);
    bool backendReady = false;

    bool Init(int width, int height) {
        w = width; h = height;
        const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
        if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
        if (FAILED(hr)) return false;
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &target))) return false;
        if (FAILED(dev->CreateRenderTargetView(target, nullptr, &rtv))) return false;
        ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);
        ImGui::GetIO().IniFilename = nullptr;
        backendReady = ImGui_ImplDX11_Init(dev, ctx);
        return backendReady;
    }

    // Runs `frames` frames of `fn` (the first ones let ImGui settle its layout) and leaves the last one in the target.
    // `mouse` (optional, x < 0 = none) parks a fake cursor so hover states can be looked at too.
    void Frame(const std::function<void()>& fn, int frames = 3, float mouseX = -1.0f, float mouseY = -1.0f) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < frames; ++i) {
            io.DisplaySize = ImVec2((float)w, (float)h);
            io.DeltaTime = 1.0f / 60.0f;
            if (mouseX >= 0) io.AddMousePosEvent(mouseX, mouseY);
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            fn();
            ImGui::Render();
            const float c[4] = { clear.x, clear.y, clear.z, clear.w };
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, c);
            D3D11_VIEWPORT vp{ 0, 0, (float)w, (float)h, 0, 1 };
            ctx->RSSetViewports(1, &vp);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }

    bool Save(const char* bmpPath) {
        D3D11_TEXTURE2D_DESC sd{}; sd.Width = w; sd.Height = h; sd.MipLevels = 1; sd.ArraySize = 1; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(dev->CreateTexture2D(&sd, nullptr, &st))) return false;
        ctx->CopyResource(st, target);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) { st->Release(); return false; }
        FILE* fp = nullptr; fopen_s(&fp, bmpPath, "wb");
        bool ok = fp != nullptr;
        if (fp) {
            const uint32_t rowBytes = (uint32_t)w * 4, imgSize = rowBytes * (uint32_t)h;
            uint8_t fh[14] = { 'B', 'M' }; const uint32_t fsz = 54 + imgSize, off = 54; memcpy(fh + 2, &fsz, 4); memcpy(fh + 10, &off, 4);
            uint8_t ih[40] = {}; const uint32_t hs = 40; memcpy(ih, &hs, 4);
            const int32_t wv = w, hv = -h; memcpy(ih + 4, &wv, 4); memcpy(ih + 8, &hv, 4);
            const uint16_t planes = 1, bpp = 32; memcpy(ih + 12, &planes, 2); memcpy(ih + 14, &bpp, 2); memcpy(ih + 20, &imgSize, 4);
            fwrite(fh, 1, 14, fp); fwrite(ih, 1, 40, fp);
            for (int y = 0; y < h; ++y) fwrite((const char*)m.pData + (size_t)y * m.RowPitch, 1, rowBytes, fp);
            fclose(fp);
        }
        ctx->Unmap(st, 0); st->Release();
        return ok;
    }

    void Shutdown() {
        if (backendReady) ImGui_ImplDX11_Shutdown();
        backendReady = false;
        if (rtv) rtv->Release(); if (target) target->Release(); if (ctx) ctx->Release(); if (dev) dev->Release();
        rtv = nullptr; target = nullptr; ctx = nullptr; dev = nullptr;
    }
};
