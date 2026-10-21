// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// v1.0.2: do not rely on bodyless "inline" functions from the 2.2091 bindings:
// in this dataset PlayLayer::get(), GameManager::get() etc. are marked
// "= inline" with no implementation, so codegen emits stubs whose behavior is
// meaningless at runtime (PlayLayer::get() kept returning null inside levels).
//
// Chain used to find the level instead:
//  1. GameManager::sharedState()->m_playLayer  (real win address 0x2762c0 +
//     a plain field from the same 2.2091 bro - always coherent)
//  2. typeinfo_cast scan of the running scene (works via the game's RTTI)
//
// Raw input: still the v1.0.1 approach - subclass the game's registered
// raw-input sink and forward everything back with CallWindowProcW, peeking
// only at side buttons 4/5. The game's own input pipeline stays untouched.

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/utils/cocos.hpp>
#include <Windows.h>

#pragma comment(lib, "user32")

using namespace geode::prelude;

namespace {
    struct RawHook {
        WNDPROC prevProc = nullptr;
        HWND hwnd = nullptr;
        bool active = false;
    };
    RawHook g_hook;

    PlayLayer* scanForPlayLayer(CCNode* node) {
        if (auto pl = typeinfo_cast<PlayLayer*>(node)) return pl;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto pl = scanForPlayLayer(child)) return pl;
        }
        return nullptr;
    }

    PlayLayer* findPlayLayer() {
        if (auto gm = GameManager::sharedState()) {
            if (auto pl = gm->m_playLayer) return pl;
        }
        if (auto scene = CCDirector::get()->getRunningScene()) {
            if (auto pl = scanForPlayLayer(scene)) return pl;
        }
        return nullptr;
    }

    bool gameIsForeground() {
        HWND fore = GetForegroundWindow();
        if (!fore) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fore, &pid);
        return pid == GetCurrentProcessId();
    }

    void onSideButtonPress() {
        bool fore = gameIsForeground();
        PlayLayer* pl = fore ? findPlayLayer() : nullptr;
        log::info(
            "side button: foreground={} inLevel={} paused={}",
            fore ? 1 : 0,
            pl ? 1 : 0,
            (pl ? pl->m_isPaused : 0)
        );
        if (pl && !pl->m_isPaused) {
            pl->pauseGame(false);
            log::info("pauseGame(false) called");
        }
    }

    LRESULT CALLBACK rawInputHookProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INPUT) {
            BYTE buf[sizeof(RAWINPUT)];
            UINT size = sizeof(buf);
            if (GetRawInputData(
                    reinterpret_cast<HRAWINPUT>(lp), RID_INPUT,
                    buf, &size, sizeof(RAWINPUTHEADER)
                ) != static_cast<UINT>(-1)
            ) {
                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buf);
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    USHORT flags = raw->data.mouse.usButtonFlags;
                    if ((flags & RI_MOUSE_BUTTON_4_DOWN) || (flags & RI_MOUSE_BUTTON_5_DOWN)) {
                        onSideButtonPress();
                    }
                }
            }
            // never consume: the game's own handler keeps its job
        }
        if (g_hook.prevProc) {
            return CallWindowProcW(g_hook.prevProc, hwnd, msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void attemptSubclassRawInput() {
        if (g_hook.active) return;

        UINT count = 0;
        GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
        if (count == 0) return;
        std::vector<RAWINPUTDEVICE> devices(count);
        if (GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == 0) return;

        for (auto const& dev : devices) {
            if (dev.usUsagePage == 0x01 && dev.usUsage == 0x02) {
                HWND target = static_cast<HWND>(dev.hwndTarget);
                if (!target) continue;
                LONG_PTR prev = GetWindowLongPtrW(target, GWLP_WNDPROC);
                if (!prev) continue;
                if (SetWindowLongPtrW(target, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(rawInputHookProc))) {
                    g_hook.prevProc = reinterpret_cast<WNDPROC>(prev);
                    g_hook.hwnd     = target;
                    g_hook.active   = true;
                    log::info(
                        "raw input sink subclassed (hwnd={} prevProc={})",
                        reinterpret_cast<uintptr_t>(target),
                        static_cast<uintptr_t>(prev)
                    );
                    return;
                }
            }
        }
    }
}

$on_mod(Loaded) {
    // Diagnostic: the loader-level pipeline (alive via the game's raw window)
    // also fires for side buttons; the shared onSideButtonPress is idempotent.
    MouseInputEvent().listen([](MouseInputData& data) {
        using Button = MouseInputData::Button;
        bool side = data.button == Button::Button4 || data.button == Button::Button5;
        if (data.action == MouseInputData::Action::Press) {
            log::info("loader-level mouse event: button={}", static_cast<int>(data.button));
        }
        if (side && data.action == MouseInputData::Action::Press) {
            onSideButtonPress();
        }
        return ListenerResult::Propagate;
    }).leak();

    queueInMainThread([] {
        attemptSubclassRawInput();
    });

    log::info("Side Button Pause v1.0.2 loaded: Mouse 4 / Mouse 5 pause the level");
}
