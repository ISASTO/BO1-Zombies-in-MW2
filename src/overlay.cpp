#include "runtime.hpp"
#include "checked_memory.hpp"
#include "zombie_bindings.hpp"
#include <algorithm>
#include <cmath>
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
        if (overlay->visible && (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_LEFT || wparam == VK_RIGHT
                                || wparam == VK_RETURN || wparam == VK_BACK || wparam == VK_ESCAPE)) {
            if (down && (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_LEFT || wparam == VK_RIGHT || !repeat)) {
                if (wparam == VK_UP) overlay->queue(Input::Up);
                else if (wparam == VK_DOWN) overlay->queue(Input::Down);
                else if (wparam == VK_LEFT) overlay->queue(Input::Left);
                else if (wparam == VK_RIGHT) overlay->queue(Input::Right);
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
void DrawHitmarker(IDirect3DDevice9* device,IDirect3DTexture9* texture,float centerX,float centerY,float scale,std::uint8_t alpha) {
    if(!texture)return;
    const float x=centerX-12.f*scale-.5f,y=centerY-12.f*scale-.5f,w=24.f*scale,h=48.f*scale;
    const auto color=D3DCOLOR_ARGB(alpha,255,255,255);
    const Vertex a{x,y,0,1,color,0,0},b{x+w,y,0,1,color,1,0},c{x,y+h,0,1,color,0,1},d{x+w,y+h,0,1,color,1,1};
    const std::array<Vertex,6> vertices{a,b,c,c,b,d};
    device->SetTexture(0,texture);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices.data(),sizeof(Vertex));
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
        case Page::ZombieRules: return "BO1 ROUND RULES PREVIEW";
        case Page::View: return "VIEW / FOV SETTINGS";
        case Page::ZombieController: return "BO1 CONTROLLER REHEARSAL";
        case Page::ZombieGame: return "PLAYABLE ZOMBIES";
    }
    return "";
}
const char* Row(Page page, int n) {
    static constexpr const char* home[] = {"PLAYER", "WEAPONS", "WORLD", "AI", "ZOMBIE MODE", "DIAGNOSTICS"};
    static constexpr const char* player[] = {"God Mode", "FOV settings", "Noclip", "Super Speed", "Super Jump", "Reset player toggles"};
    static constexpr const char* weapons[] = {"Give All (usable banks)", "Infinite Ammo", "No Reload", "Infinite Grenades (linked)", "Rapid Fire", "Explosive Bullets", "Force Dual Wield", "No Recoil", "Hitmarkers", "Make Semi-Auto Full-Auto", "Weapon info to log", "Reset weapon toggles"};
    static constexpr const char* world[] = {"World Speed", "Freeze AI", "Gravity", "Kill All Enemies", "Allow Friendly Fire", "Reset world toggles"};
    static constexpr const char* ai[] = {"Spawn Enemy (nearest point)", "Spawn Friendly (near you)", "Enemy model", "Friendly model", "Pause AI spawning", "Freeze AI (linked)", "Invincible Spawned Friendlies", "Alive enemy / friendly", "Our spawned actors", "Remove our spawned actors", "AI info to log", "Engine actor slots"};
    static constexpr const char* zombies[] = {"Round rules preview", "Controller rehearsal", "Playable zombies"};
    static constexpr const char* diag[] = {"Status HUD", "Write State to Log"};
    static constexpr const char* rules[] = {"Preview round", "Total zombies", "Zombie health", "Simultaneous cap", "Spawn interval", "Between rounds"};
    static constexpr const char* view[] = {"Base FOV", "Save for next launch", "Restore default / clear saved", "Live base FOV", "Saved base FOV"};
    static constexpr const char* game[] = {"Start / End zombies", "Spec Ops map", "Starting round", "Hellhounds", "Campaign allies", "Round", "Points", "Zombies left", "Kills / headshots", "Powerups", "BO1 anims / models", "Zombies info to log"};
    static constexpr const char* controller[] = {"Start / End rehearsal", "Starting round", "Auto-clear simulated actors", "Pause spawn queue", "Simulate one kill", "Clear simulated alive", "Current round", "Simulated alive", "Unspawned queue", "Phase / timer", "Zombie health", "Controller info to log"};
    switch (page) {
        case Page::Home: return MenuText<Page::Home>(home,n); case Page::Player: return MenuText<Page::Player>(player,n);
        case Page::Weapons: return MenuText<Page::Weapons>(weapons,n); case Page::World: return MenuText<Page::World>(world,n);
        case Page::AI: return MenuText<Page::AI>(ai,n); case Page::Zombies: return MenuText<Page::Zombies>(zombies,n);
        case Page::Diagnostics: return MenuText<Page::Diagnostics>(diag,n);
        case Page::ZombieRules: return MenuText<Page::ZombieRules>(rules,n);
        case Page::View: return MenuText<Page::View>(view,n);
        case Page::ZombieController: return MenuText<Page::ZombieController>(controller,n);
        case Page::ZombieGame: return MenuText<Page::ZombieGame>(game,n);
    }
    return "";
}
void LogRoundPreview(int round) {
    const auto p = bo1::EvaluateNormalSoloRound(round, round == 1);
    if (!p) return;
    Log("BO1_RULES source=LOCAL_CSV_PLUS_GSC_FALLBACK tuning_csv=PRESENT column=2 profile=NORMAL_SOLO "
        "round=%d first_round=%d total=%d health=%d concurrent_limit=%d spawn_interval=%.9f between_round_seconds=%d",
        p->round, round == 1, p->total, p->health, p->concurrentLimit, p->spawnInterval, p->betweenRoundSeconds);
}
// Projects a world point with the player's view (playerState viewangles,
// eye height and cg_fov, which is the horizontal field of view at 4:3).
bool ProjectToScreen(const Snapshot& s,const AiPoint& point,float width,float height,float& x,float& y,float& distance) {
    if(!s.client.valid || !s.fov.valid || !s.entity) return false;
    AiPoint origin{},angles{};float eyeHeight=0.f;
    if(!CheckedReadMemory(s.entity+kAiOrigin,origin.data(),sizeof(origin)) || !CheckedReadMemory(s.client.address+kClientViewAngles,angles.data(),sizeof(angles))
        || !CheckedReadMemory(s.client.address+kClientViewHeight,&eyeHeight,sizeof(eyeHeight)) || !FinitePoint(origin) || !FinitePoint(angles)
        || !std::isfinite(eyeHeight) || std::abs(eyeHeight)>200.f) return false;
    origin[2]+=eyeHeight;
    const float rad=.017453292f;
    const float sp=std::sin(angles[0]*rad),cp=std::cos(angles[0]*rad),sy=std::sin(angles[1]*rad),cy=std::cos(angles[1]*rad),sr=std::sin(angles[2]*rad),cr=std::cos(angles[2]*rad);
    const AiPoint forward{cp*cy,cp*sy,-sp};
    const AiPoint right{-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
    const AiPoint up{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    const AiPoint d{point[0]-origin[0],point[1]-origin[1],point[2]-origin[2]};
    auto dot=[&](const AiPoint& a){return a[0]*d[0]+a[1]*d[1]+a[2]*d[2];};
    const float z=dot(forward);distance=std::sqrt(PointDistanceSquared(point,origin));
    if(z<4.f) return false;
    const float tanY=std::tan(std::clamp(s.fov.current,1.f,170.f)*.5f*rad)*.75f,tanX=tanY*(width/height);
    x=width*.5f+dot(right)/z/tanX*width*.5f; y=height*.5f-dot(up)/z/tanY*height*.5f;
    return std::isfinite(x) && std::isfinite(y) && x>-200.f && x<width+200.f && y>-200.f && y<height+200.f;
}
const char* ZombieMapText(int index,char* buffer,std::size_t size,const char* current) {
    if(index<0) std::snprintf(buffer,size,"This level%s%.20s",current && current[0]?": ":"",current?current:"");
    else std::snprintf(buffer,size,"%s",kSpecOpsMaps[static_cast<std::size_t>(index)].title);
    return buffer;
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
    InvalidateMpFeedback();
    if(hitTexture){hitTexture->Release();hitTexture=nullptr;}hitAttempted=false;
    if (font) { font->Release(); font = nullptr; }
    fontAttempted = false;
}
void Overlay::frame() {
    lastRenderThread = GetCurrentThreadId();
    if (!settingsLoaded) { LoadFovPreference(menu.fov); settingsLoaded=true; }
    std::deque<Input> events;
    { std::lock_guard<std::mutex> lock(inputMutex); events.swap(inputQueue); }
    for (const auto event : events) {
        ++inputs;
        const Action action = menu.input(event);
        Log("INPUT key=%d visible=%d page=%d selected=%d", static_cast<int>(event), menu.visible,
            static_cast<int>(menu.page), menu.selected);
        if (action == Action::ToggleGod) TogglePlayerFeature(PlayerFeature::God);
        else if (action == Action::ToggleInfiniteAmmo) TogglePlayerFeature(PlayerFeature::InfiniteAmmo);
        else if (action == Action::ToggleNoReload) {
            if (menu.page!=Page::Weapons || menu.selected!=3 || ReadSnapshot().grenadeSustainResolved) TogglePlayerFeature(PlayerFeature::NoReload);
            else Log("GRENADE_REQUEST blocked: native frag/flash branches unresolved");
        }
        else if (action == Action::ToggleNoclip) TogglePlayerFeature(PlayerFeature::Noclip);
        else if (action == Action::ToggleSuperSpeed) TogglePlayerFeature(PlayerFeature::SuperSpeed);
        else if (action == Action::ToggleSuperJump) TogglePlayerFeature(PlayerFeature::SuperJump);
        else if (action == Action::ResetPlayer) ResetPlayerFeatures();
        else if (action == Action::DecreaseSpeed || action == Action::IncreaseSpeed)
            AdjustPlayerFeature(PlayerFeature::SuperSpeed,action==Action::IncreaseSpeed?1:-1);
        else if (action == Action::DecreaseJump || action == Action::IncreaseJump)
            AdjustPlayerFeature(PlayerFeature::SuperJump,action==Action::IncreaseJump?1:-1);
        else if (action == Action::ToggleGiveAll) ToggleExtraFeature(ExtraFeature::GiveAll);
        else if (action == Action::ToggleDualWield) ToggleExtraFeature(ExtraFeature::ForceDualWield);
        else if (action == Action::PreviousWeaponBank || action == Action::NextWeaponBank)
            AdjustWeaponBank(action==Action::NextWeaponBank?1:-1);
        else if (action == Action::ToggleRapidFire) ToggleExtraFeature(ExtraFeature::RapidFire);
        else if (action == Action::DecreaseRapidFire || action == Action::IncreaseRapidFire)
            AdjustExtraFeature(ExtraFeature::RapidFire,action==Action::IncreaseRapidFire?1:-1);
        else if (action == Action::ToggleNoRecoil) ToggleExtraFeature(ExtraFeature::NoRecoil);
        else if (action == Action::ToggleHitmarkers) ToggleExtraFeature(ExtraFeature::Hitmarkers);
        else if (action == Action::ToggleSemiAutoFullAuto) ToggleExtraFeature(ExtraFeature::SemiAutoFullAuto);
        else if (action == Action::ToggleInvincibleFriendlies) ToggleExtraFeature(ExtraFeature::InvincibleFriendlies);
        else if (action == Action::ToggleExplosiveBullets) ToggleExtraFeature(ExtraFeature::ExplosiveBullets);
        else if (action == Action::ResetWeapons) ResetWeaponFeatures();
        else if (action == Action::ToggleWorldSpeed) ToggleExtraFeature(ExtraFeature::WorldSpeed);
        else if (action == Action::ToggleFreezeAI) ToggleExtraFeature(ExtraFeature::FreezeAI);
        else if (action == Action::ToggleGravity) ToggleExtraFeature(ExtraFeature::Gravity);
        else if (action == Action::ToggleFriendlyFire) ToggleExtraFeature(ExtraFeature::FriendlyFire);
        else if (action == Action::ResetWorld) ResetWorldFeatures();
        else if(action==Action::SpawnAiEnemy) RequestAiOperation(AiOperation::SpawnEnemy,ReadSnapshot());
        else if(action==Action::SpawnAiFriendly) RequestAiOperation(AiOperation::SpawnFriendly,ReadSnapshot());
        else if(action==Action::KillAllEnemies) RequestAiOperation(AiOperation::KillEnemies,ReadSnapshot());
        else if(action==Action::RemoveSpawnedAi) RequestAiOperation(AiOperation::RemoveOwned,ReadSnapshot());
        else if(action==Action::PreviousEnemySpawner || action==Action::NextEnemySpawner)
            AdjustAiSpawner(false,action==Action::NextEnemySpawner?1:-1);
        else if(action==Action::PreviousFriendlySpawner || action==Action::NextFriendlySpawner)
            AdjustAiSpawner(true,action==Action::NextFriendlySpawner?1:-1);
        else if(action==Action::ToggleAiSpawnPause) ToggleAiSpawnPause();
        else if(action==Action::WriteAiState) LogAiState();
        else if (action == Action::DecreaseTimeScale || action == Action::IncreaseTimeScale)
            AdjustExtraFeature(ExtraFeature::WorldSpeed,action==Action::IncreaseTimeScale?1:-1);
        else if (action == Action::DecreaseGravity || action == Action::IncreaseGravity)
            AdjustExtraFeature(ExtraFeature::Gravity,action==Action::IncreaseGravity?1:-1);
        else if (action == Action::WriteSnapshot) LogSnapshot();
        else if (action == Action::WriteRoundPreview) LogRoundPreview(menu.previewRound);
        else if(IsRehearsalAction(action)) HandleZombieRehearsal(action,ReadSnapshot(),menu.rehearsalRound);
        else if(action==Action::StartZombies) {
            const auto zs=GetZombieGameStatus();
            if(zs.running || zs.pending) RequestZombieStop();
            else {
                ZombieSettings settings; settings.startingRound=menu.zombieRound; settings.hellhounds=menu.zombieHellhounds; settings.removeAllies=menu.zombieRemoveAllies;
                const auto snap=ReadSnapshot();
                const char* map=menu.zombieMap<0?nullptr:kSpecOpsMaps[static_cast<std::size_t>(menu.zombieMap)].name;
                // "This level" on a Spec Ops map loads it again so the level is
                // prepared (BO1 animations, Spec Ops end screens disabled).
                if(!map && snap.state==LevelState::Ready && SpecOpsMapIndex(snap.map)>=0) map=kSpecOpsMaps[static_cast<std::size_t>(SpecOpsMapIndex(snap.map))].name;
                bool started=false;
                if(map) started=LaunchZombieMap(map,settings);
                else if(snap.state==LevelState::Ready) { RequestZombieStart(settings,nullptr); started=true; }
                Log("ZOMBIE_MENU start map=%s round=%d hellhounds=%d remove_allies=%d accepted=%d",map?map:snap.map,settings.startingRound,settings.hellhounds,settings.removeAllies,started);
                if(started) menu.visible=false;
            }
        }
        else if(action==Action::ToggleZombieHellhounds) SetZombieHellhounds(menu.zombieHellhounds);
        else if(action==Action::WriteZombieState) LogZombieGame();
        else if(action==Action::ChangeZombieMap || action==Action::ChangeZombieRound || action==Action::ToggleZombieAllies)
            Log("ZOMBIE_MENU map=%d round=%d hellhounds=%d remove_allies=%d",menu.zombieMap,menu.zombieRound,menu.zombieHellhounds,menu.zombieRemoveAllies);
        else if (action == Action::ChangeFov) {
            nextFovAttempt=0; settingsResult="Session FOV selected";
            Log("FOV_PREF selected=%d saved=%d active=1",menu.fov.selected,menu.fov.saved.value_or(0));
        } else if (action == Action::SaveFov) {
            if (SaveFovPreference(menu.fov.selected)) {
                menu.fov.savedSuccessfully(); nextFovAttempt=0; settingsResult="Saved for next launch";
            } else settingsResult="Save failed; see log";
        } else if (action == Action::ResetFov) {
            if (ClearFovPreference()) {
                menu.fov.defaultSuccessfullyCleared(); nextFovAttempt=0; settingsResult="Saved preference cleared";
            } else settingsResult="Clear failed; see log";
        }
    }
    {
        std::lock_guard<std::mutex> lock(inputMutex);
        auto predicted = menu;
        for (const auto event : inputQueue) predicted.input(event);
        visible = predicted.visible;
    }
    const ULONGLONG now = GetTickCount64();
    UpdatePlayerFeatures(now);
    if(!events.empty() || now>=nextSnapshotRead) {displaySnapshot=ReadSnapshot();nextSnapshotRead=now+50;}
    Snapshot s = displaySnapshot;
    UpdateAiControls(s,now,menu.visible && menu.page==Page::AI);
    const auto aiStatus=GetAiStatus();
    UpdateZombieRehearsal(s);
    const auto rehearsal=GetZombieRehearsalStatus();
    TickZombieRelaunch();
    const auto zombie=GetZombieGameStatus();
    const auto playerPreferences=GetPlayerPreferences();
    const auto extraPreferences=GetExtraPreferences();
    const auto weaponMode=GetWeaponModeStatus();
    if (menu.fov.desired && s.state==LevelState::Ready && s.fov.valid && now>=nextFovAttempt) {
        nextFovAttempt=now+250;
        if (SetFov(*menu.fov.desired)) { menu.fov.applied(); s=ReadSnapshot(); }
    }
    if (!lastHeartbeat || now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        Log("HEARTBEAT present=%u endscene=%u draws=%u inputs=%u resets=%u visible=%d render_thread=%lu",
            presents, endScenes, draws, inputs, resets, menu.visible, lastRenderThread);
        LogSnapshot();
        LogZombieRehearsal();
        LogZombieGame();
        LogAiState();
        Log("FOV_PREF selected=%d saved=%d active=%d reset_pending=%d",
            menu.fov.selected,menu.fov.saved.value_or(0),menu.fov.desired.has_value(),menu.fov.releaseAfterDefault);
        Log("PLAYER_PREF god=%d infinite_ammo=%d no_reload=%d noclip=%d super_speed=%d super_jump=%d session_only=1",
            playerPreferences.wanted(PlayerFeature::God),playerPreferences.wanted(PlayerFeature::InfiniteAmmo),
            playerPreferences.wanted(PlayerFeature::NoReload),playerPreferences.wanted(PlayerFeature::Noclip),
            playerPreferences.wanted(PlayerFeature::SuperSpeed),playerPreferences.wanted(PlayerFeature::SuperJump));
        Log("EXTRA_PREF speed_selected=%d jump_selected=%d give_all=%d rapid_fire=%d explosive=%d world_speed=%d time_selected=%.2f freeze_ai=%d gravity=%d gravity_selected=%d dual_wield=%d bank=%d/%d granted=%d bank_owned=%d dual_owned=%d session_only=1",
            playerPreferences.speed,playerPreferences.jump,extraPreferences.wanted(ExtraFeature::GiveAll),extraPreferences.wanted(ExtraFeature::RapidFire),
            extraPreferences.wanted(ExtraFeature::ExplosiveBullets),extraPreferences.wanted(ExtraFeature::WorldSpeed),extraPreferences.timeScale,
            extraPreferences.wanted(ExtraFeature::FreezeAI),extraPreferences.wanted(ExtraFeature::Gravity),extraPreferences.gravity,
            extraPreferences.wanted(ExtraFeature::ForceDualWield),weaponMode.page+1,weaponMode.pages,weaponMode.added,weaponMode.owned,weaponMode.dualOwned);
        Log("WEAPON_OPTIONS rapid_multiplier=%d no_recoil=%d resolved=%d valid=%d native_scale=%d native_flag=%d hitmarkers=%d semi_auto_full_auto=%d fire_mode=%d owned_mode=%d",
            extraPreferences.rapidFireMultiplier,extraPreferences.wanted(ExtraFeature::NoRecoil),s.recoilResolved,s.recoilValid,s.recoilScale,(s.weaponFlags&kRecoilScaleFlag)!=0,
            extraPreferences.wanted(ExtraFeature::Hitmarkers),extraPreferences.wanted(ExtraFeature::SemiAutoFullAuto),s.weapon.fireMode,SemiAutoApplied(s.weapon));
    }
    PrepareMpFeedback(*this);
    const auto hitAlpha=GetHitmarkerAlpha(now);
    static int seenGameOver=0; static ULONGLONG gameOverAt=0;
    if(zombie.gameOverRound!=seenGameOver) { seenGameOver=zombie.gameOverRound; gameOverAt=now; }
    const bool showGameOver=zombie.gameOverRound>0 && !zombie.running && now-gameOverAt<10000;
    if (!menu.visible && !menu.hud && !hitAlpha && !zombie.running && !showGameOver) return;
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
    if ((menu.visible || menu.hud) && !font && !fontAttempted) BuildFont(*this);
    Configure(device);
    const float panelHeight=MenuPanelHeight(menu.count());
    const float scale=std::min(1.1f,std::min(size.Width/710.f,size.Height/(panelHeight+76.f)));
    auto rect = [&](float x,float y,float w,float h,D3DCOLOR c) { Rect(device,x*scale,y*scale,w*scale,h*scale,c); };
    auto text = [&](float x,float y,D3DCOLOR c,const char* value,float factor=.8f) { Text(*this,x*scale,y*scale,scale*factor,c,value); };
    char line[128]{};
    if(hitAlpha)DrawHitmarker(device,hitTexture,size.Width*.5f,size.Height*.5f-std::trunc(MpCrosshairOffset()*240.f)*(size.Height/480.f),size.Height/480.f,hitAlpha);
    if (menu.hud) {
        rect(12,12,446,25,D3DCOLOR_ARGB(205,7,10,6));
        std::snprintf(line,sizeof(line),"BO1MW2 | F4 | %s",StateText(s.state));
        text(20,13,kAccent,line,.7f);
        if(rehearsal.controller.running() && !menu.visible) {
            rect(12,38,610,25,D3DCOLOR_ARGB(205,7,10,6));
            std::snprintf(line,sizeof(line),"BO1 REHEARSAL | R%d | Virtual %d | Queue %d | %s",rehearsal.controller.round,
                rehearsal.controller.alive,rehearsal.controller.queued,bo1::PhaseText(rehearsal.controller.phase));
            text(20,39,kAccent,line,.7f);
        }
    }
    // BO1-style zombies HUD: round bottom-left, points bottom-right, active
    // powerups bottom-centre, dropped powerups marked where they lie.
    if(zombie.running || showGameOver) {
        const float W=static_cast<float>(size.Width),H=static_cast<float>(size.Height),u=H/480.f;
        const D3DCOLOR blood=D3DCOLOR_ARGB(235,170,18,12),white=D3DCOLOR_ARGB(240,236,232,220),gold=D3DCOLOR_ARGB(240,236,198,92);
        char hud[96]{};
        if(zombie.running) {
            std::snprintf(hud,sizeof(hud),"%d",zombie.round.round);
            Text(*this,18.f*u,H-66.f*u,1.7f*u,blood,hud);
            Text(*this,20.f*u,H-82.f*u,.6f*u,blood,zombie.round.dogs?"HELLHOUNDS":"ROUND");
            std::snprintf(hud,sizeof(hud),"%d",zombie.points);
            const float pw=11.f*1.1f*u*static_cast<float>(std::strlen(hud));
            Rect(device,W-pw-34.f*u,H-58.f*u,pw+22.f*u,34.f*u,D3DCOLOR_ARGB(150,0,0,0));
            Text(*this,W-pw-23.f*u,H-56.f*u,1.1f*u,gold,hud);
            if(zombie.pointsDeltaAgeMs>=0 && zombie.pointsDeltaAgeMs<900 && zombie.lastPointsDelta>0) {
                std::snprintf(hud,sizeof(hud),"+%d",zombie.lastPointsDelta);
                Text(*this,W-pw-23.f*u-60.f*u,H-74.f*u-zombie.pointsDeltaAgeMs*.02f*u,.8f*u,gold,hud);
            }
            float px=W*.5f-120.f*u;
            if(zombie.doublePointsMs>0) { std::snprintf(hud,sizeof(hud),"x2 %ds",(zombie.doublePointsMs+999)/1000); Text(*this,px,H-40.f*u,.8f*u,gold,hud); px+=110.f*u; }
            if(zombie.instaKillMs>0) { std::snprintf(hud,sizeof(hud),"INSTA %ds",(zombie.instaKillMs+999)/1000); Text(*this,px,H-40.f*u,.8f*u,white,hud); }
            if(zombie.round.phase==bo1::RoundPhase::Between) {
                std::snprintf(hud,sizeof(hud),"Next round in %llds",static_cast<long long>((zombie.round.waitMilliseconds+999)/1000));
                Text(*this,20.f*u,H-110.f*u,.6f*u,white,hud);
            }
            for(int d=0;d<zombie.dropCount;++d) {
                const auto& drop=zombie.drops[static_cast<std::size_t>(d)];
                float x=0,y=0,distance=0;
                if(!drop.visible || !ProjectToScreen(s,drop.origin,W,H,x,y,distance)) continue;
                Rect(device,x-7.f*u,y-7.f*u,14.f*u,14.f*u,D3DCOLOR_ARGB(220,90,230,90));
                std::snprintf(hud,sizeof(hud),"%s %.0fm",bo1::PowerupText(drop.type),distance*.0254f);
                Text(*this,x-60.f*u,y-26.f*u,.6f*u,D3DCOLOR_ARGB(240,150,255,150),hud);
            }
        } else {
            std::snprintf(hud,sizeof(hud),"GAME OVER  -  You survived %d round%s",zombie.gameOverRound,zombie.gameOverRound==1?"":"s");
            Text(*this,W*.5f-11.f*1.2f*u*static_cast<float>(std::strlen(hud))*.5f,H*.32f,1.2f*u,blood,hud);
            if(zombie.restartInMs>=0) {
                std::snprintf(hud,sizeof(hud),"New game in %d s  (F4 > Start to cancel)",(zombie.restartInMs+999)/1000);
                Text(*this,W*.5f-11.f*.7f*u*static_cast<float>(std::strlen(hud))*.5f,H*.32f+40.f*u,.7f*u,D3DCOLOR_ARGB(230,235,225,200),hud);
            }
        }
    }
    if (menu.visible) {
        rect(24,54,660,panelHeight,D3DCOLOR_ARGB(240,10,13,9));
        rect(24,54,660,3,kAccent);
        text(40,66,kAccent,"MW2 / BO1 ZOMBIES",.95f);
        text(40,96,kText,PageName(menu.page));
        if (menu.page == Page::ZombieRules) {
            text(40,121,kMuted,"Local BO1 CSV + script fallbacks",.7f);
            std::snprintf(line,sizeof(line),"Normal solo rounds | Preview %d / %d", menu.previewRound, bo1::kPreviewMaxRound);
            text(40,143,kMuted,line,.7f);
        } else if(menu.page==Page::ZombieController) {
            text(40,121,kMuted,"Simulated actor adapter | No native actors",.7f);
            if(rehearsal.controller.running())
                std::snprintf(line,sizeof(line),"R%d | Virtual %d | Queue %d | %s",rehearsal.controller.round,
                    rehearsal.controller.alive,rehearsal.controller.queued,bo1::PhaseText(rehearsal.controller.phase));
            else std::snprintf(line,sizeof(line),"Clock: commandTime | Stop: %s",bo1::ReasonText(rehearsal.controller.reason));
            text(40,143,kMuted,line,.7f);
        } else if(menu.page==Page::ZombieGame) {
            text(40,121,kMuted,!zombie.available?"Native zombie bindings unavailable; see log":
                zombie.running?(zombie.levelPrepared?"Running | Spec Ops map | BO1 animations":"Running | BO1 rules on native MW2 actors"):
                zombie.pending?"Loading the Spec Ops map...":zombie.restartInMs>=0?"GAME OVER | new game soon (Start cancels)":"Ready | Zombies rise at the map's enemy spawns",.7f);
            std::snprintf(line,sizeof(line),"Map: %.24s | Dog spawners: %s",s.map[0]?s.map:"main menu",zombie.dogSpawners?"yes":"none");
            text(40,143,kMuted,line,.7f);
        } else if(menu.page==Page::AI) {
            std::snprintf(line,sizeof(line),"Native mission AI | %s | Recovering: %d",AiResultText(aiStatus.lastResult),aiStatus.recovering);
            text(40,121,kMuted,line,.7f);
            const bool friendly=menu.selected==1 || menu.selected==3;
            if(aiStatus.lastEntity)
                std::snprintf(line,sizeof(line),"Last spawn #%u | %.0f units away | %d tries",aiStatus.lastEntity,aiStatus.lastSpawnDistance,aiStatus.placementAttempts);
            else if(friendly && aiStatus.friendlySpawners)
                std::snprintf(line,sizeof(line),"Near you | %.38s",aiStatus.friendlyName.data());
            else if(aiStatus.enemySpawners)
                std::snprintf(line,sizeof(line),"Nearest point %.0f units | %.28s",aiStatus.enemyDistance,aiStatus.enemyName.data());
            else std::snprintf(line,sizeof(line),"No eligible loaded mission templates");
            text(40,143,kMuted,line,.7f);
        } else if (menu.page==Page::View) {
            text(40,121,kMuted,StateText(s.state),.7f);
            text(40,143,kMuted,"Game default: 65 | Selector: 60-120",.7f);
        } else if (s.map[0]) {
            text(40,121,kMuted,StateText(s.state),.7f);
            if (s.playerRead == PlayerReadStatus::Complete)
                std::snprintf(line,sizeof(line),"Map: %.32s   Health: %d",s.map,s.health);
            else std::snprintf(line,sizeof(line),"Map: %.32s   Health: UNKNOWN",s.map);
            text(40,143,kMuted,line,.7f);
        } else {
            text(40,121,kMuted,StateText(s.state),.7f);
            text(40,143,kMuted,"Test 16 | Scan cost + Ghost tag repair",.7f);
        }
        const auto preview = menu.page == Page::ZombieRules
            ? bo1::EvaluateNormalSoloRound(menu.previewRound, menu.previewRound == 1) : std::nullopt;
        const int first=menu.firstVisibleRow();
        for (int n=first; n<menu.count(); ++n) {
            const float y=170.f+(n-first)*26.f;
            const bool selected=menu.selected==n;
            if (selected) { rect(34,y-1,640,24,D3DCOLOR_ARGB(255,49,57,32)); rect(34,y-1,3,24,kAccent); }
            if(menu.page==Page::AI && (n==2 || n==3)) {
                const char* name=n==2?aiStatus.enemyName.data():aiStatus.friendlyName.data();
                if(n==2 && std::strncmp(name,"actor_enemy_",12)==0)name+=12;
                std::snprintf(line,sizeof(line),"%s model: %.45s",n==2?"Enemy":"Friendly",name[0]?name:"None");
                text(45,y+1,selected?kAccent:kText,line,.64f);
            } else text(45,y+1,selected?kAccent:kText,Row(menu.page,n));
            const char* status="PLANNED";
            char value[32]{};
            if (menu.page==Page::Home) status=">";
            else if (menu.page==Page::Player && n==0) {
                if (s.state!=LevelState::Ready) status="LOCKED";
                else if (s.god) status=playerPreferences.wanted(PlayerFeature::God)?"ON":"RESETTING";
                else if ((s.flags&2) || (s.client.valid && ((s.client.otherFlags&1) || (s.client.cheatFlags&3)))
                    || (s.damageRead && !s.takesDamage)) status="GUARDED";
                else if (playerPreferences.wanted(PlayerFeature::God)) status="WAIT";
                else status=s.client.valid && s.damageRead?"OFF":"OFF*";
            }
            else if (menu.page==Page::Diagnostics) status=n==0?(menu.hud?"ON":"OFF"):"ENTER";
            else if (menu.page==Page::Zombies) status=n<2 || zombie.available?"ENTER":"LOCKED";
            else if(menu.page==Page::ZombieGame) {
                const auto& c=zombie.round;
                if(n==0) status=!zombie.available?"LOCKED":zombie.running?"END":zombie.pending?"CANCEL":menu.zombieMap<0 && s.state!=LevelState::Ready?"PICK MAP":"START";
                else if(n==1) status=ZombieMapText(menu.zombieMap,value,sizeof(value),s.map);
                else if(n==2) { std::snprintf(value,sizeof(value),"%d",menu.zombieRound); status=value; }
                else if(n==3) status=!menu.zombieHellhounds?"OFF":zombie.dogSpawners?"ON":"ON (NO DOGS)";
                else if(n==4) status=menu.zombieRemoveAllies?"REMOVE":"KEEP";
                else if(n==5) { if(zombie.running) std::snprintf(value,sizeof(value),"%d %s",c.round,bo1::PhaseText(c.phase)); else std::snprintf(value,sizeof(value),"-"); status=value; }
                else if(n==6) { std::snprintf(value,sizeof(value),"%d",zombie.points); status=zombie.running?value:"-"; }
                else if(n==7) { std::snprintf(value,sizeof(value),"%d + %d",c.alive,c.queued); status=zombie.running?value:"-"; }
                else if(n==8) { std::snprintf(value,sizeof(value),"%u / %u",zombie.kills,zombie.headshots); status=zombie.running?value:"-"; }
                else if(n==9) {
                    if(!zombie.running) status="-";
                    else { std::snprintf(value,sizeof(value),"%s%s%d out",zombie.doublePointsMs>0?"x2 ":"",zombie.instaKillMs>0?"INSTA ":"",zombie.dropCount); status=value; }
                }
                else if(n==10) {
                    if(zombie.levelPrepared && zombie.modelsPrecached) std::snprintf(value,sizeof(value),"A%d/%d M%d SO%d",zombie.animSlotsApplied,zombie.animSlotsWanted,zombie.modelsPrecached,zombie.specOpsGuards);
                    else if(zombie.levelPrepared) std::snprintf(value,sizeof(value),"A%d/%d M-%.8s SO%d",zombie.animSlotsApplied,zombie.animSlotsWanted,zombie.bo1Models,zombie.specOpsGuards);
                    else std::snprintf(value,sizeof(value),"%.8s %d/%d M %.8s",zombie.bo1Assets,zombie.bo1AnimsFound,zombie.bo1AnimsWanted,zombie.bo1Models);
                    status=value;
                }
                else status="ENTER";
            }
            else if(menu.page==Page::ZombieController) {
                const auto& c=rehearsal.controller;
                if(n==0) status=c.running()?"END":rehearsal.ready?"START":"LOCKED";
                else if(n==1) { std::snprintf(value,sizeof(value),"%d",menu.rehearsalRound); status=value; }
                else if(n==2) status=rehearsal.autoClear?"ON":"OFF";
                else if(n==3) status=!c.running()?"LOCKED":c.spawnEnabled?"OFF":"ON";
                else if(n==4 || n==5) status=c.alive>0?"ENTER":"EMPTY";
                else if(n==6) { std::snprintf(value,sizeof(value),"%d",c.round); status=value; }
                else if(n==7) { std::snprintf(value,sizeof(value),"%d / %d",c.alive,bo1::kConcurrentLimit); status=value; }
                else if(n==8) { std::snprintf(value,sizeof(value),"%d",c.queued); status=value; }
                else if(n==9) { std::snprintf(value,sizeof(value),"%s %.1fs",bo1::PhaseText(c.phase),c.waitMilliseconds/1000.0); status=value; }
                else if(n==10) { std::snprintf(value,sizeof(value),"%d",c.health); status=value; }
                else status="ENTER";
            }
            else if (menu.page==Page::Player && n==1) status=">";
            else if (menu.page==Page::Player && n>=2 && n<=4) {
                const auto feature=static_cast<PlayerFeature>(n+1);
                const bool wanted=playerPreferences.wanted(feature);
                if (!FeatureAvailable(s,feature)) status="LOCKED";
                else if (feature==PlayerFeature::InfiniteAmmo) status=wanted?"ON":"OFF";
                else if (feature==PlayerFeature::NoReload) status=s.sustain.current?(wanted?"ON":"NATIVE"):(wanted?"WAIT":"OFF");
                else if (feature==PlayerFeature::Noclip) status=(s.client.cheatFlags&1)?"ON":(wanted?"WAIT":"OFF");
                else if (feature==PlayerFeature::SuperSpeed) {
                    const char* state=wanted?(s.speed.current==playerPreferences.speed?"ON":"WAIT"):"OFF";
                    std::snprintf(value,sizeof(value),"%s %.1fx",state,playerPreferences.speed/static_cast<float>(kDefaultSpeed)); status=value;
                } else if (feature==PlayerFeature::SuperJump) {
                    const char* state=wanted?(s.jump.height==playerPreferences.jump?"ON":"WAIT"):(s.jump.height==kDefaultJump?"OFF":"WAIT");
                    std::snprintf(value,sizeof(value),"%s %dx",state,playerPreferences.jump/static_cast<int>(kDefaultJump)); status=value;
                }
            }
            else if (menu.page==Page::Player && n==5) status="ENTER";
            else if (menu.page==Page::Weapons) {
                if (n==0) {
                    if (weaponMode.needsBaseline) status="RESTART";
                    else if (!ExtraAvailable(s,ExtraFeature::GiveAll)) status="LOCKED";
                    else if(!weaponMode.pages) status="WAIT";
                    else { std::snprintf(value,sizeof(value),"%s %d/%d",extraPreferences.wanted(ExtraFeature::GiveAll)?(weaponMode.owned && !weaponMode.reconciling?"ON":"WAIT"):(weaponMode.owned?"UNDO":"OFF"),weaponMode.page+1,weaponMode.pages); status=value; }
                }
                else if (n==1) status=!FeatureAvailable(s,PlayerFeature::InfiniteAmmo)?"LOCKED":playerPreferences.wanted(PlayerFeature::InfiniteAmmo)?"ON":"OFF";
                else if (n==2 || n==3) status=(n==3 && !s.grenadeSustainResolved) || !FeatureAvailable(s,PlayerFeature::NoReload)?"LOCKED":s.sustain.current?(playerPreferences.wanted(PlayerFeature::NoReload)?"ON":"NATIVE"):(playerPreferences.wanted(PlayerFeature::NoReload)?"WAIT":"OFF");
                else if (n==4) {
                    const char* state=!ExtraAvailable(s,ExtraFeature::RapidFire)?"LOCKED":extraPreferences.wanted(ExtraFeature::RapidFire)?(extraPreferences.rapidFireMultiplier==1 || RapidFireApplied(s.weapon)?"ON":"WAIT"):"OFF";
                    std::snprintf(value,sizeof(value),"%s %dx",state,extraPreferences.rapidFireMultiplier);status=value;
                }
                else if (n==5) status=!ExtraAvailable(s,ExtraFeature::ExplosiveBullets)?"LOCKED":s.explosive.current?(extraPreferences.wanted(ExtraFeature::ExplosiveBullets)?"ON":"NATIVE"):(extraPreferences.wanted(ExtraFeature::ExplosiveBullets)?"WAIT":"OFF");
                else if (n==6) status=!ExtraAvailable(s,ExtraFeature::ForceDualWield)?"LOCKED":extraPreferences.wanted(ExtraFeature::ForceDualWield)?(weaponMode.dualReady && s.dualWield.current && s.lastWeaponHand==1?(weaponMode.dualFallback?(weaponMode.nativeHandState==NativeHandBuildState::Queued?"BUILD":"COMPAT"):"ON"):s.dualCurrentSupported?"WAIT":"ON*"):weaponMode.dualOwned?"UNDO":s.dualWield.current?"NATIVE":"OFF";
                else if (n==7) status=!ExtraAvailable(s,ExtraFeature::NoRecoil)?"LOCKED":extraPreferences.wanted(ExtraFeature::NoRecoil)?(s.recoilScale==0 && (s.weaponFlags&kRecoilScaleFlag)?"ON":"WAIT"):"OFF";
                else if (n==8) status=!MpFeedbackReady()?"ASSET MISSING":!ExtraAvailable(s,ExtraFeature::Hitmarkers)?"LOCKED":extraPreferences.wanted(ExtraFeature::Hitmarkers)?"ON":"OFF";
                else if (n==9) status=!ExtraAvailable(s,ExtraFeature::SemiAutoFullAuto)?"LOCKED":!extraPreferences.wanted(ExtraFeature::SemiAutoFullAuto)?"OFF":s.weapon.fireMode==0?"ON":s.weapon.fireMode==1?"WAIT":"ON (BURST)";
                else status="ENTER";
            }
            else if (menu.page==Page::World) {
                if (n==0) {
                    const bool wanted=extraPreferences.wanted(ExtraFeature::WorldSpeed);
                    if (!ExtraAvailable(s,ExtraFeature::WorldSpeed)) status="LOCKED";
                    else { std::snprintf(value,sizeof(value),"%s %.1fx",wanted?(s.timeScale.current==extraPreferences.timeScale?"ON":"WAIT"):"OFF",extraPreferences.timeScale); status=value; }
                } else if (n==1) status=!ExtraAvailable(s,ExtraFeature::FreezeAI)?"LOCKED":!s.ai.current?(extraPreferences.wanted(ExtraFeature::FreezeAI)?"ON":"NATIVE"):(extraPreferences.wanted(ExtraFeature::FreezeAI)?"WAIT":"OFF");
                else if (n==2) {
                    const bool wanted=extraPreferences.wanted(ExtraFeature::Gravity);
                    if (!ExtraAvailable(s,ExtraFeature::Gravity)) status="LOCKED";
                    else { std::snprintf(value,sizeof(value),"%s %d",wanted?(s.gravityLiteral==extraPreferences.gravity+.5f && s.gravity.current==extraPreferences.gravity?"ON":"WAIT"):"OFF",extraPreferences.gravity); status=value; }
                } else if (n==3) status=!aiStatus.ready?"LOCKED":aiStatus.pending?"QUEUED":aiStatus.enemyAlive?"ENTER":"EMPTY";
                else if(n==4) status=!ExtraAvailable(s,ExtraFeature::FriendlyFire)?"LOCKED":extraPreferences.wanted(ExtraFeature::FriendlyFire)?(s.friendlyFireDistance.current==0.f && s.friendlyFireScale.current==1.f?"ON":"WAIT"):"OFF";
                else status="ENTER";
            }
            else if(menu.page==Page::AI) {
                if(n==0 || n==1) status=!aiStatus.ready?"LOCKED":aiStatus.pending?"QUEUED":
                    !(n==1?aiStatus.friendlySpawners:aiStatus.enemySpawners)?"NONE":(n==1 && !aiStatus.characterLoaded)?"NOT LOADED":aiStatus.spawnPaused?"PAUSED":aiStatus.engineActors>=NativeActorCapacity()?"ENGINE FULL":"ENTER";
                else if(n==2) { std::snprintf(value,sizeof(value),"%d/%d",aiStatus.enemySpawner,aiStatus.enemySpawners); status=value; }
                else if(n==3) { std::snprintf(value,sizeof(value),"%d/%d",aiStatus.friendlySpawner,aiStatus.friendlySpawners); status=value; }
                else if(n==4) status=!aiStatus.ready?"LOCKED":aiStatus.spawnPaused?(aiStatus.pauseWanted?"ON":"NATIVE"):(aiStatus.pauseWanted?"WAIT":"OFF");
                else if(n==5) status=!ExtraAvailable(s,ExtraFeature::FreezeAI)?"LOCKED":!s.ai.current?"ON":"OFF";
                else if(n==6) status=!ExtraAvailable(s,ExtraFeature::InvincibleFriendlies)?"LOCKED":extraPreferences.wanted(ExtraFeature::InvincibleFriendlies)?"ON":"OFF";
                else if(n==7) { std::snprintf(value,sizeof(value),"%d / %d",aiStatus.enemyAlive,aiStatus.friendlyAlive); status=aiStatus.ready?value:"UNKNOWN"; }
                else if(n==8) { std::snprintf(value,sizeof(value),"%d",aiStatus.owned); status=value; }
                else if(n==9) status=aiStatus.pending?"QUEUED":aiStatus.owned?"ENTER":"EMPTY";
                else if(n==11) { std::snprintf(value,sizeof(value),"%d / %d",aiStatus.engineActors,NativeActorCapacity()); status=value; }
                else status="ENTER";
            }
            else if (menu.page==Page::View) {
                if (n==0) std::snprintf(value,sizeof(value),"%d",menu.fov.selected);
                else if (n==1 || n==2) std::snprintf(value,sizeof(value),"ENTER");
                else if (n==3) {
                    if (s.fov.valid) std::snprintf(value,sizeof(value),"%.1f",s.fov.current);
                    else std::snprintf(value,sizeof(value),"UNKNOWN");
                } else if (menu.fov.saved) std::snprintf(value,sizeof(value),"%d",*menu.fov.saved);
                else std::snprintf(value,sizeof(value),"NONE");
                status=value;
            }
            else if (preview) {
                switch (n) {
                    case 0: std::snprintf(value,sizeof(value),"%d",preview->round); break;
                    case 1: std::snprintf(value,sizeof(value),"%d",preview->total); break;
                    case 2: std::snprintf(value,sizeof(value),"%d",preview->health); break;
                    case 3: std::snprintf(value,sizeof(value),"%d",preview->concurrentLimit); break;
                    case 4: std::snprintf(value,sizeof(value),"%.3fs",preview->spawnInterval); break;
                    case 5: std::snprintf(value,sizeof(value),"%ds",preview->betweenRoundSeconds); break;
                }
                status=value;
            }
            text(552,y+3,kMuted,status,.7f);
        }
        const float divider=170.f+menu.count()*26.f+5.f;
        rect(34,divider,640,1,D3DCOLOR_ARGB(255,57,63,47));
        text(40,divider+7,kMuted,menu.page==Page::ZombieRules
            ? "LEFT/RIGHT: Round  ENTER: Next  UP/DOWN: Row" : menu.page==Page::ZombieController
            ? "LEFT/RIGHT: Start round  UP/DOWN: Row" : menu.page==Page::View
            ? "LEFT/RIGHT: FOV  UP/DOWN: Row  ENTER: Act" : (menu.page==Page::Player || menu.page==Page::World)
            ? "LEFT/RIGHT: Value  ENTER: Toggle  UP/DOWN: Row" : menu.page==Page::Weapons
            ? "LEFT/RIGHT: Bank / Rate  ENTER: Toggle" : menu.page==Page::AI
            ? "LEFT/RIGHT: Template  ENTER: Act  UP/DOWN: Row" : menu.page==Page::ZombieGame
            ? "LEFT/RIGHT: Map / Round  ENTER: Act  UP/DOWN: Row" : "UP/DOWN: Select  ENTER: Toggle / Open",.7f);
        text(40,divider+29,kMuted,"BACKSPACE/ESC: Back   F4: Close",.7f);
        const char* footer="Game keeps running. WASD and mouse work.";
        if (menu.page==Page::ZombieRules) footer="Read-only preview. No zombies spawned.";
        if(menu.page==Page::Zombies) footer=zombie.available?"Playable zombies: BO1 rules on native MW2 actors.":"Controller rehearsal ready. Native actors pending.";
        if(menu.page==Page::ZombieGame) {
            static constexpr const char* hints[]={
                "Loads the Spec Ops map, clears its AI, USP .45, BO1 rounds.",
                "LEFT/RIGHT: Spec Ops map. ENTER loads it (also from main menu).",
                "LEFT/RIGHT: 1-100. BO1 normal solo counts and health.",
                "BO1 dog rounds; needs a map with dog spawners.",
                "REMOVE deletes Price, Soap etc. KEEP lets them fight.",
                "Rounds, intro and 10 s break follow BO1 timings.",
                "500 start, 10 per hit, 50-130 per kill (x2 doubles).",
                "Alive now + still to spawn this round (24 at once).",
                "Headshot kills give the 50 point bonus.",
                "Kaboom, insta-kill, double points, max ammo.",
                "BO1 anims (A) and models (M) from your Black Ops install.",
                "Logs round, points, powerups and native results."
            };
            footer=MenuText<Page::ZombieGame>(hints,menu.selected);
            if(!zombie.available) footer="Exact native zombie bindings unavailable; see log.";
        }
        if(menu.page==Page::ZombieController) {
            static constexpr const char* hints[]={
                "Rehearsal only. Campaign AI and assets are untouched.",
                "LEFT/RIGHT: 1-100. Applies at the next START.",
                "Debug adapter clears one virtual actor per game second.",
                "Gates new requests; existing virtual actors remain.",
                "Reports one simulated death. Queue is unchanged.",
                "Clears alive only; queued obligations still spawn.",
                "Normal solo rules; special rounds are excluded.",
                "24 simultaneous slots. No native zombies spawned.",
                "Spent only after a simulated creation succeeds.",
                "Game clock drives intro, spawning and round delays.",
                "Recovered normal CSV + patched BO1 health calculation.",
                "Logs counts, timers, callbacks and budget invariant."
            };
            footer=MenuText<Page::ZombieController>(hints,menu.selected);
        }
        if (menu.page==Page::Player) {
            static constexpr const char* hints[]={
                "GUARDED = other native protection, including noclip.",
                "FOV can be saved. Player toggles start OFF on launch.",
                                "WASD + mouse to fly; SPACE up, crouch key down.",
                "LEFT/RIGHT: 1x-5x. ENTER: speed ON/OFF.",
                "LEFT/RIGHT: 1x-8x. Land before reducing jump.",
                "Movement/protection OFF; weapon ammo options remain."
            };
            footer=MenuText<Page::Player>(hints,menu.selected);
            if (menu.selected==0 && !s.client.valid && s.state==LevelState::Ready)
                footer="OFF* = additional protection fields unavailable.";
        }
        if (menu.page==Page::Weapons) {
            static constexpr const char* hints[]={
                "LEFT/RIGHT: Bank. OFF restores inventory and ammo.",
                "Maintains reserve ammo; magazines still reload.",
                "Stops normal gun clip consumption; reload key works.",
                "Linked to No Reload above. Frag/flash grenades.",
                "LEFT/RIGHT: 1x-10x. ENTER: Toggle; shared gun timing.",
                "All bullet weapons, including AI. Try with God ON.",
                "COMPAT: normal animations. BUILD: native hand setup.",
                "Native player recoil scale is zero; OFF restores it.",
                "Shows an X when your shot damages an actor; fades quickly.",
                "Hold fire to repeat single shots at their native rate.",
                "Records current weapon and new controls in latest.log.",
                "Weapon cheats OFF, including ammo, reload and grenade sustain."
            };
            footer=MenuText<Page::Weapons>(hints,menu.selected);
            if (menu.selected==0 && weaponMode.needsBaseline) footer="Restart mission once for a clean inventory baseline.";
            if(menu.selected==6 && extraPreferences.wanted(ExtraFeature::ForceDualWield) && !weaponMode.dualReady) {
                std::snprintf(line,sizeof(line),"Dual pending: %s",DualBlockedText(weaponMode.dualBlocked)); footer=line;
            } else if(menu.selected==6 && extraPreferences.wanted(ExtraFeature::ForceDualWield) && weaponMode.dualFallback) {
                footer=weaponMode.nativeHandsReady?"Distinct native hand definitions ready; normal animations.":
                    weaponMode.nativeHandState==NativeHandBuildState::Failed?"Native hand setup failed; compatibility definitions remain.":
                    weaponMode.nativeHandState==NativeHandBuildState::Queued?"Native hand setup queued; awaits the next native server frame.":
                    "Compatibility active; native hand setup awaits the next native server frame.";
            }
        }
        if (menu.page==Page::World) {
            static constexpr const char* hints[]={
                "LEFT/RIGHT: 0.1x-10x. ENTER: ON/OFF; whole game.",
                "Pauses all actor thinking. Player and scripts continue.",
                "LEFT/RIGHT: 100-1600. Player + projectile gravity.",
                "Native damage to hostile NPCs; allies/player excluded.",
                "Allied hits bypass the player friendly-fire penalty.",
                "Restores world overrides; player/FOV choices remain."
            };
            footer=MenuText<Page::World>(hints,menu.selected);
        }
        if(menu.page==Page::AI) {
            static constexpr const char* hints[]={
                "Nearest point, clearance retries, then advance to you.",
                "Clear nearby ground; ally follows and fights enemies.",
                "LEFT/RIGHT: model. Spawn point stays automatic.",
                "LEFT/RIGHT: Soldier / Ghost / Price / Soap / Roach.",
                "Native spawn gate; existing actors still think.",
                "Linked to WORLD Freeze AI. Spawn gate is separate.",
                "Our living allies ignore damage, including friendly fire.",
                "Verified live axis/allies actors; other teams excluded.",
                "No eight-actor cap. Native pool capacity still applies.",
                "Our NPCs and corpses; original mission NPCs remain.",
                "Logs placement, team, goal and native hand-tree results.",
                "Shared actor pool; campaign NPCs also use these slots."
            };
            footer=MenuText<Page::AI>(hints,menu.selected);
            if(menu.selected==3 && !aiStatus.characterLoaded)
                footer="Character assets are not loaded in this mission; choose Soldier.";
            else if(menu.selected==3 && aiStatus.friendlyFallback)
                footer="Soldier uses a loaded human model with native ally AI.";
            if((menu.selected==0 || menu.selected==1) && aiStatus.lastResult==AiRequestResult::NativeRefused)
                footer="Native creation refused; see diagnostic log.";
            if((menu.selected==0 || menu.selected==1) && aiStatus.lastResult==AiRequestResult::NoClearPosition)
                footer="No clear nearby ground; move or choose another model.";
            if(!aiStatus.resolved) footer="Exact native AI profile unavailable; see log.";
        }
        if (menu.page==Page::View) {
            if (!s.fov.resolved || !s.fov.valid) footer="FOV unavailable; see log.";
            else if (menu.fov.desired && s.state!=LevelState::Ready) footer="FOV queued until player is alive.";
            else if (settingsResult[0]) footer=settingsResult;
            else footer="Save keeps this FOV for future launches.";
        }
        text(40,divider+55,kAccent,footer,.7f);
    }
    state->Apply();
    if (SUCCEEDED(worldResult)) device->SetTransform(D3DTS_WORLD,&world);
    if (SUCCEEDED(viewResult)) device->SetTransform(D3DTS_VIEW,&view);
    if (SUCCEEDED(projectionResult)) device->SetTransform(D3DTS_PROJECTION,&projection);
    if (SUCCEEDED(viewportResult)) device->SetViewport(&savedViewport);
    state->Release();
    ++draws;
    if (draws==1) Log("DRAW first overlay frame backbuffer=%ux%u font=%d menu_scale=%.2f panel=%.0fx%.0f visible_rows=%d",size.Width,size.Height,font!=nullptr,
        scale,660*scale,panelHeight*scale,menu.count());
}
} // namespace mod
