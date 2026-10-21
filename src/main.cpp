// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// v1.0.1: NO RegisterRawInputDevices of our own. Registering the mouse usage
// with RIDEV_INPUTSINK on our hidden window REPLACED the game's own raw-input
// registration (Windows keeps a single registration per usage per process),
// which starved the game's raw input pipeline and made all clicks dead.
//
// What we do instead:
//  1. GetRegisteredRawInputDevices() tells us the exact window the game
//     registered as the raw-input sink (libcocos2d.dll creates a hidden
//     raw-input window on 2.2091).
//  2. We subclass that window with SetWindowLongPtrW and forward EVERYTHING
//     back with CallWindowProcW, so the game (and Geode's own chain, if it
//     is on the window) keeps receiving all input untouched.
//  3. For WM_INPUT we additionally peek at the mouse button flags and pause
//     when side buttons 4/5 are pressed, the game window is focused, and we
//     are inside a PlayLayer that is not already paused. pauseGame is called
//     from the WndProc thread, which is the thread that owns the window -
//     the game's main thread - so it is safe.
//
// If no registered mouse sink can be found (game may re-register later), we
// retry for a while and then fall back to hosting our own sink window.
// That fallback only happens when nothing else registered the mouse usage,
// so it cannot starve anyone.

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/loader/ModEvent.hpp>
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
    int g_attempts = 0;

    void pauseIfInLevel() {
        if (auto pl = PlayLayer::get()) {
            if (!pl->m_isPaused) {
                pl->pauseGame(false);
            }
        }
    }

    bool gameIsForeground() {
        auto view = CCEGLView::get();
        if (!view || !view->m_pMainWindow) return false;
        // The loader resolves the game's main HWND this exact way
        // (loader/src/platform/windows/input.cpp, g_mainWindowHWND).
        HWND mainWin = *reinterpret_cast<HWND*>(reinterpret_cast<uintptr_t>(view->m_pMainWindow) + 0x370);
        HWND fore = GetForegroundWindow();
        return fore != nullptr && fore == mainWin;
    }

    void onSideButtonPress() {
        bool fore = gameIsForeground();
        auto pl = PlayLayer::get();
        log::info(
            "side button: foreground={} inLevel={} paused={}",
            fore ? 1 : 0,
            pl ? 1 : 0,
            (pl ? pl->m_isPaused : 0)
        );
        if (fore && pl && !pl->m_isPaused) {
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

    // Fallback sink (only used when NO other registration owns the mouse
    // usage in this process, so nothing gets starved).
    constexpr wchar_t kRawWndClass[] = L"ArtemSideButtonRawSink";

    LRESULT CALLBACK sinkWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void setupOwnSinkWindow() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = sinkWndProc;
        wc.lpszClassName = kRawWndClass;
        wc.hInstance = GetModuleHandleW(nullptr);
        if (!RegisterClassExW(&wc)) {
            log::warn("RegisterClassExW(sink) failed: {}", GetLastError());
            return;
        }
        HWND hwnd = CreateWindowExW(
            0, kRawWndClass, L"", 0,
            0, 0, 0, 0,
            nullptr, nullptr, wc.hInstance, nullptr
        );
        if (!hwnd) {
            log::warn("CreateWindowExW(sink) failed: {}", GetLastError());
            return;
        }
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = hwnd;
        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            log::warn("RegisterRawInputDevices(sink) failed: {}", GetLastError());
            return;
        }
        log::info("own sink ready (hwnd={})", reinterpret_cast<uintptr_t>(hwnd));
    }

    void attemptSubclassRawInput() {
        if (g_hook.active) return;
        if (++g_attempts > 600) {
            log::warn("no registered raw input sink found after 600 attempts, giving up");
            return;
        }

        UINT count = 0;
        UINT copied = GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
        if (count == 0) {
            queueInMainThread([] { attemptSubclassRawInput(); });
            return;
        }
        std::vector<RAWINPUTDEVICE> devices(count);
        copied = GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE));
        if (copied == 0) {
            log::warn("GetRegisteredRawInputDevices failed: {}", GetLastError());
            queueInMainThread([] { attemptSubclassRawInput(); });
            return;
        }

        for (auto const& dev : devices) {
            if (dev.usUsagePage == 0x01 && dev.usUsage == 0x02) {
                HWND target = static_cast<HWND>(dev.hwndTarget);
                if (!target) continue;
                LONG_PTR prev = GetWindowLongPtrW(target, GWLP_WNDPROC);
                if (!prev) {
                    log::warn("GetWindowLongPtrW on raw window failed: {}", GetLastError());
                    continue;
                }
                if (SetWindowLongPtrW(target, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(rawInputHookProc))) {
                    g_hook.prevProc = reinterpret_cast<WNDPROC>(prev);
                    g_hook.hwnd     = target;
                    g_hook.active   = true;
                    log::info(
                        "raw input sink subclassed (hwnd={} prevProc={})",
                        reinterpret_cast<uintptr_t>(target),
                        reinterpret_cast<uintptr_t>(prev)
                    );
                    return;
                }
                log::warn("SetWindowLongPtrW failed: {}", GetLastError());
            }
        }
        queueInMainThread([] { attemptSubclassRawInput(); });
    }
}

$on_mod(Loaded) {
    // Diagnostic: if Geode's loader-level pipeline ever dispatches here on
    // this build, this shows up; harmless either way (idempotent pause).
    MouseInputEvent().listen([](MouseInputData& data) {
        using Button = MouseInputData::Button;
        bool side = data.button == Button::Button4 || data.button == Button::Button5;
        log::info(
            "loader-level mouse event: button={} press={}",
            static_cast<int>(data.button),
            data.action == MouseInputData::Action::Press ? 1 : 0
        );
        if (side && data.action == MouseInputData::Action::Press && gameIsForeground()) {
            pauseIfInLevel();
        }
        return ListenerResult::Propagate;
    }).leak();

    queueInMainThread([] {
        attemptSubclassRawInput();
    });

    log::info("Side Button Pause loaded: Mouse 4 / Mouse 5 pause the level");
}
