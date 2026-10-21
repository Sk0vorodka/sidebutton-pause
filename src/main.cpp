// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// Why the DIY raw-input bridge below:
//  On GD 2.2091 the game does not create the hidden "GD_RawInput" window that
//  Geode's own raw-input pipeline (MouseInputEvent) depends on, so the loader
//  level pipeline never dispatches mouse events here. We therefore register
//  our own hidden window as the raw-input sink for the mouse and read
//  RI_MOUSE_BUTTON_4/5_DOWN ourselves. The WndProc runs on the main thread
//  (the window is created on it), so calling PlayLayer::pauseGame directly
//  is safe.
//
// RIDEV_INPUTSINK means we receive clicks even when the game window is not
// foreground; we compensate by checking GetForegroundWindow() against the
// game's main window before pausing (same way the loader resolves it).

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/loader/ModEvent.hpp>
#include <Windows.h>

using namespace geode::prelude;

namespace {
    constexpr wchar_t kRawWndClass[] = L"ArtemSideButtonRawInput";

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
        // The loader resolves the game's main HWND exactly this way
        // (loader/src/platform/windows/input.cpp, g_mainWindowHWND).
        HWND mainWin = *reinterpret_cast<HWND*>(reinterpret_cast<uintptr_t>(view->m_pMainWindow) + 0x370);
        HWND fore = GetForegroundWindow();
        return fore != nullptr && fore == mainWin;
    }

    LRESULT CALLBACK rawWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
                    bool sideDown =
                        (flags & RI_MOUSE_BUTTON_4_DOWN) != 0 ||
                        (flags & RI_MOUSE_BUTTON_5_DOWN) != 0;
                    if (sideDown) {
                        bool fore = gameIsForeground();
                        auto pl = PlayLayer::get();
                        log::info(
                            "side button raw input: foreground={} inLevel={} paused={}",
                            fore ? 1 : 0,
                            pl ? 1 : 0,
                            (pl ? pl->m_isPaused : 0)
                        );
                        if (fore && pl && !pl->m_isPaused) {
                            pl->pauseGame(false);
                            log::info("pauseGame(false) called");
                        }
                    }
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void setupRawInputWindow() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = rawWndProc;
        wc.lpszClassName = kRawWndClass;
        wc.hInstance = GetModuleHandleW(nullptr);
        if (!RegisterClassExW(&wc)) {
            log::warn("RegisterClassExW failed: {}", GetLastError());
            return;
        }

        // Normal (non message-only) hidden window: message-only windows are
        // unreliable as raw input sinks.
        HWND hwnd = CreateWindowExW(
            0, kRawWndClass, L"", 0,
            0, 0, 0, 0,
            nullptr, nullptr, wc.hInstance, nullptr
        );
        if (!hwnd) {
            log::warn("CreateWindowExW failed: {}", GetLastError());
            return;
        }

        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // generic desktop controls
        rid.usUsage     = 0x02; // mouse
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = hwnd;
        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            log::warn("RegisterRawInputDevices failed: {}", GetLastError());
            return;
        }

        log::info("raw input bridge ready (hwnd={})", static_cast<uintptr_t>(hwnd));
    }
}

$on_mod(Loaded) {
    // The loader-level event may never fire on this GD build; it also serves
    // as a diagnostic if it does.
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

    // The WndProc must live on the main thread, and the game window must be
    // up before we resolve it, so run the bridge setup on the main thread.
    queueInMainThread([] {
        setupRawInputWindow();
    });

    log::info("Side Button Pause loaded: Mouse 4 / Mouse 5 pause the level");
}
