#include "runtime.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

namespace mod {
namespace {
std::mutex windowMutex;
std::map<HWND, Overlay*> windows;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Overlay* overlay = nullptr;
    { std::lock_guard<std::mutex> lock(windowMutex); auto i = windows.find(window); if (i != windows.end()) overlay = i->second; }
    if (!overlay || !overlay->previousWndProc) return DefWindowProcA(window, message, wparam, lparam);
    if (message == WM_KILLFOCUS) {
        // Close only the mod menu; never alter the game's pause dvar.
        if (overlay->visible) overlay->queue(Input::Toggle);
    }
    if (overlay->visible && message == WM_CHAR && (wparam == 13 || wparam == 8 || wparam == 27)) return 0;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP) {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool repeat = (lparam & (1LL << 30)) != 0;
        if (wparam == VK_F4 && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP) {
            if (down && !repeat) overlay->queue(Input::Toggle);
            return 0;
        }
        if (overlay->visible && (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_RETURN || wparam == VK_BACK || wparam == VK_ESCAPE)) {
            if (down && (wparam == VK_UP || wparam == VK_DOWN || !repeat)) {
                if (wparam == VK_UP) overlay->queue(Input::Up);
                else if (wparam == VK_DOWN) overlay->queue(Input::Down);
                else if (wparam == VK_RETURN) overlay->queue(Input::Select);
                else overlay->queue(Input::Back);
            }
            // Let releases clear any key the player had held before opening the menu.
            if (down) return 0;
        }
    }
    // WASD, mouse, firing, and all unrelated inputs reach the game normally.
    return CallWindowProcA(overlay->previousWndProc, window, message, wparam, lparam);
}
struct Vertex { float x, y, z, rhw; D3DCOLOR color; float u, v; };
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr D3DCOLOR kText = D3DCOLOR_ARGB(255, 231, 234, 222);
constexpr D3DCOLOR kMuted = D3DCOLOR_ARGB(255, 150, 155, 143);
constexpr D3DCOLOR kAccent = D3DCOLOR_ARGB(255, 194, 212, 133);

bool BuildFont(Overlay& overlay) {
    overlay.fontAttempted = true;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 256;
    info.bmiHeader.biHeight = -256;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    if (!bitmap || !font || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (font) DeleteObject(font);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HGDIOBJ oldFont = SelectObject(dc, font);
    std::memset(pixels, 0, 256 * 256 * 4);
    SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, RGB(0, 0, 0)); SetBkMode(dc, OPAQUE);
    for (int n = 0; n < 95; ++n) { char c = static_cast<char>(n + 32); TextOutA(dc, (n % 16) * 16, (n / 16) * 28, &c, 1); }
    GdiFlush();
    HRESULT hr = overlay.device->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &overlay.font, nullptr);
    if (SUCCEEDED(hr)) {
        D3DLOCKED_RECT lock{};
        hr = overlay.font->LockRect(0, &lock, nullptr, 0);
        if (SUCCEEDED(hr)) {
            auto* source = static_cast<unsigned char*>(pixels);
            for (unsigned y = 0; y < 256; ++y) {
                auto* dest = reinterpret_cast<DWORD*>(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch);
                for (unsigned x = 0; x < 256; ++x) {
                    unsigned char* pixel = source + (y * 256 + x) * 4;
                    const auto alpha = std::max(pixel[0], std::max(pixel[1], pixel[2]));
                    dest[x] = (static_cast<DWORD>(alpha) << 24) | 0x00ffffff;
                }
            }
            overlay.font->UnlockRect(0);
        }
        if (FAILED(hr)) { overlay.font->Release(); overlay.font = nullptr; }
    }
    SelectObject(dc, oldFont); SelectObject(dc, oldBitmap);
    DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    Log("FONT atlas creation hr=0x%08lx texture=%p (Windows font rasterized locally)", hr, overlay.font);
    return overlay.font != nullptr;
}
void Rect(IDirect3DDevice9* device, float x, float y, float width, float height, D3DCOLOR color) {
    Vertex vertices[4] = {{x-.5f,y-.5f,0,1,color,0,0}, {x+width-.5f,y-.5f,0,1,color,0,0},
                          {x-.5f,y+height-.5f,0,1,color,0,0}, {x+width-.5f,y+height-.5f,0,1,color,0,0}};
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
}
void Text(Overlay& overlay, float x, float y, float scale, D3DCOLOR color, const char* text) {
    if (!overlay.font) return;
    std::vector<Vertex> vertices;
    vertices.reserve(std::strlen(text) * 6);
    for (; *text; ++text) {
        unsigned c = static_cast<unsigned char>(*text);
        if (c < 32 || c > 126) c = '?';
        const unsigned n = c - 32;
        float u = static_cast<float>((n % 16) * 16) / 256.f;
        float v = static_cast<float>((n / 16) * 28) / 256.f;
        const float w = 16 * scale, h = 28 * scale;
        Vertex a{x-.5f,y-.5f,0,1,color,u,v}, b{x+w-.5f,y-.5f,0,1,color,u+16/256.f,v};
        Vertex d{x-.5f,y+h-.5f,0,1,color,u,v+28/256.f}, e{x+w-.5f,y+h-.5f,0,1,color,u+16/256.f,v+28/256.f};
        vertices.insert(vertices.end(), {a,b,d,d,b,e});
        x += 11 * scale;
    }
    if (!vertices.empty()) {
        overlay.device->SetTexture(0, overlay.font);
        overlay.device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        overlay.device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        overlay.device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        overlay.device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        overlay.device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size()/3), vertices.data(), sizeof(Vertex));
    }
}
const char* PageName(Page page) {
    switch (page) {
        case Page::Home: return "MOD FRAMEWORK";
        case Page::Player: return "PLAYER";
        case Page::Weapons: return "WEAPONS";
        case Page::World: return "WORLD";
        case Page::AI: return "AI";
        case Page::Zombies: return "ZOMBIE MODE";
        case Page::Diagnostics: return "DIAGNOSTICS";
    }
    return "";
}
const char* Row(Page page, int n) {
    static constexpr const char* home[] = {"PLAYER", "WEAPONS", "WORLD", "AI", "ZOMBIE MODE", "DIAGNOSTICS"};
    static constexpr const char* player[] = {"God Mode", "Infinite Ammo", "No Reload", "Noclip", "Super Speed", "Super Jump"};
    static constexpr const char* weapons[] = {"Give All", "Infinite Grenades", "Rapid Fire", "Explosive Bullets", "Debug Features"};
    static constexpr const char* world[] = {"Slow Motion", "Freeze AI", "Gravity", "Kill All Enemies"};
    static constexpr const char* ai[] = {"Spawn Enemy", "Spawn Friendly", "AI Controls"};
    static constexpr const char* zombies[] = {"Enable Zombies", "Start Game", "Round / Population Controls"};
    static constexpr const char* diag[] = {"Status HUD", "Write State to Log"};
    switch (page) {
        case Page::Home: return home[n]; case Page::Player: return player[n];
        case Page::Weapons: return weapons[n]; case Page::World: return world[n];
        case Page::AI: return ai[n]; case Page::Zombies: return zombies[n];
        case Page::Diagnostics: return diag[n];
    }
    return "";
}
void Configure(IDirect3DDevice9* device) {
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr); device->SetFVF(kFvf);
    device->SetRenderState(D3DRS_ZENABLE, FALSE); device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD); device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_LIGHTING, FALSE); device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPING, TRUE); device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf); device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}
}
void Overlay::queue(Input input) {
    std::lock_guard<std::mutex> lock(inputMutex);
    if (inputQueue.size() >= 64) return;
    inputQueue.push_back(input);
    if (input == Input::Toggle) visible = !visible.load();
}
void Overlay::attach(IDirect3DDevice9* d, HWND w) {
    if (window != w && window) detach();
    device = d;
    window = w;
    if (!window || !IsWindow(window)) { Log("INPUT no valid HWND for device=%p", device); return; }
    {
        std::lock_guard<std::mutex> lock(windowMutex);
        const auto found = windows.find(window);
        if (found != windows.end()) { Log("INPUT window already subclassed=%p", window); window = nullptr; return; }
        windows[window] = this;
        SetLastError(0);
        previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
        if (!previousWndProc) windows.erase(window);
    }
    Log("INPUT WndProc installed=%d hwnd=%p previous=%p error=%lu", previousWndProc != nullptr, window,
        reinterpret_cast<void*>(previousWndProc), GetLastError());
}
void Overlay::detach() {
    visible = false;
    if (!window || !previousWndProc) return;
    std::lock_guard<std::mutex> lock(windowMutex);
    if (IsWindow(window) && GetWindowLongPtrA(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(WindowProc)) {
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousWndProc));
        windows.erase(window);
    }
    window = nullptr;
}
void Overlay::invalidate() {
    if (font) { font->Release(); font = nullptr; }
    fontAttempted = false;
}
void Overlay::frame() {
    lastRenderThread = GetCurrentThreadId();
    std::deque<Input> events;
    { std::lock_guard<std::mutex> lock(inputMutex); events.swap(inputQueue); }
    for (const auto event : events) {
        ++inputs;
        const Action action = menu.input(event);
        Log("INPUT key=%d visible=%d page=%d selected=%d", static_cast<int>(event), menu.visible,
            static_cast<int>(menu.page), menu.selected);
        if (action == Action::ToggleGod) ToggleGod();
        else if (action == Action::WriteSnapshot) LogSnapshot();
    }
    {
        std::lock_guard<std::mutex> lock(inputMutex);
        auto predicted = menu;
        for (const auto event : inputQueue) predicted.input(event);
        visible = predicted.visible;
    }
    const Snapshot s = ReadSnapshot();
    const ULONGLONG now = GetTickCount64();
    if (!lastHeartbeat || now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        Log("HEARTBEAT present=%u endscene=%u draws=%u inputs=%u resets=%u visible=%d render_thread=%lu",
            presents, endScenes, draws, inputs, resets, menu.visible, lastRenderThread);
        LogSnapshot();
    }
    if (!menu.visible && !menu.hud) return;
    IDirect3DStateBlock9* state = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state)) || !state) return;
    if (FAILED(state->Capture())) { state->Release(); return; }
    D3DVIEWPORT9 savedViewport{};
    D3DMATRIX world{}, view{}, projection{};
    const HRESULT viewportResult = device->GetViewport(&savedViewport);
    const HRESULT worldResult = device->GetTransform(D3DTS_WORLD, &world);
    const HRESULT viewResult = device->GetTransform(D3DTS_VIEW, &view);
    const HRESULT projectionResult = device->GetTransform(D3DTS_PROJECTION, &projection);
    IDirect3DSurface9* target = nullptr;
    D3DSURFACE_DESC size{};
    if (FAILED(device->GetRenderTarget(0, &target)) || !target) { state->Apply(); state->Release(); return; }
    HRESULT sizeResult = target->GetDesc(&size); target->Release();
    if (FAILED(sizeResult) || !size.Width || !size.Height) { state->Apply(); state->Release(); return; }
    D3DVIEWPORT9 full{0,0,size.Width,size.Height,0,1}; device->SetViewport(&full);
    if (!font && !fontAttempted) BuildFont(*this);
    Configure(device);
    const float scale = std::max(.5f, std::min(1.6f, std::min(size.Width/1280.f, size.Height/720.f)));
    auto rect = [&](float x,float y,float w,float h,D3DCOLOR c) { Rect(device,x*scale,y*scale,w*scale,h*scale,c); };
    auto text = [&](float x,float y,D3DCOLOR c,const char* value,float factor=1.f) { Text(*this,x*scale,y*scale,scale*factor,c,value); };
    char line[128]{};
    if (menu.hud) {
        rect(18,18,540,31,D3DCOLOR_ARGB(205,7,10,6));
        std::snprintf(line,sizeof(line),"BO1MW2 | F4 | %s",StateText(s.state));
        text(27,21,kAccent,line,.85f);
    }
    if (menu.visible) {
        rect(40,74,570,506,D3DCOLOR_ARGB(240,10,13,9));
        rect(40,74,570,4,kAccent);
        text(61,92,kAccent,"MW2 / BO1 ZOMBIES",1.15f);
        text(61,125,kText,PageName(menu.page));
        text(61,156,kMuted,StateText(s.state),.88f);
        if (s.map[0]) {
            std::snprintf(line,sizeof(line),"Map: %.32s   Health: %d",s.map,s.health);
            text(61,180,kMuted,line,.88f);
        } else text(61,180,kMuted,"Menu test 3 | Zombies under development",.88f);
        for (int n=0; n<menu.count(); ++n) {
            const float y=216.f+n*42.f;
            const bool selected=menu.selected==n;
            if (selected) { rect(52,y-2,546,37,D3DCOLOR_ARGB(255,49,57,32)); rect(52,y-2,3,37,kAccent); }
            text(67,y+3,selected?kAccent:kText,Row(menu.page,n));
            const char* status="PLANNED";
            if (menu.page==Page::Home) status=">";
            else if (menu.page==Page::Player && n==0) status=s.state==LevelState::Ready?(s.god?"ON":"OFF"):"LOCKED";
            else if (menu.page==Page::Diagnostics) status=n==0?(menu.hud?"ON":"OFF"):"ENTER";
            text(503,y+5,kMuted,status,.85f);
        }
        rect(52,483,546,1,D3DCOLOR_ARGB(255,57,63,47));
        text(61,497,kMuted,"UP/DOWN: Select  ENTER: Activate",.88f);
        text(61,522,kMuted,"BACKSPACE/ESC: Back   F4: Close",.88f);
        text(61,551,kAccent,"Game keeps running. WASD and mouse work.",.85f);
    }
    state->Apply();
    if (SUCCEEDED(worldResult)) device->SetTransform(D3DTS_WORLD,&world);
    if (SUCCEEDED(viewResult)) device->SetTransform(D3DTS_VIEW,&view);
    if (SUCCEEDED(projectionResult)) device->SetTransform(D3DTS_PROJECTION,&projection);
    if (SUCCEEDED(viewportResult)) device->SetViewport(&savedViewport);
    state->Release();
    ++draws;
    if (draws==1) Log("DRAW first overlay frame backbuffer=%ux%u font=%d",size.Width,size.Height,font!=nullptr);
}
} // namespace mod
