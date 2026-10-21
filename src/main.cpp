// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// v1.0.3 - the working architecture after several on-device rounds:
//
//  * Input: Geode's loader-level MouseInputEvent works on this build (the
//    game's raw-input window lives in libcocos2d.dll and the loader's
//    pipeline dispatches MouseInputData with Button4/Button5). We do NOT
//    touch WndProc or RegisterRawInputDevices at all.
//
//  * Level detection: the 2.2091 bindings dataset contains bodyless
//    "= inline" stubs (PlayLayer::get(), GameManager::get()), so nothing
//    that relies on them can be used. We typeinfo_cast-scan the running
//    scene for the active PlayLayer instead (the game's real RTTI keeps
//    this correct in menus, levels and editor lifecycles).
//
//  * Safety: pauseGame is queued to the next main-thread tick
//    (queueInMainThread), gated on
//     - a PauseLayer already on the scene (typeinfo scan),
//     - PlayLayer::m_isPaused,
//     - GJBaseGameLayer::m_loadingLayer == null && m_started == true
//    so we never pause during level loading, death animation, outro or
//    while the pause menu is already open - those were the states that
//    produced the broken pause background and a driver crash.

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Windows.h>

#pragma comment(lib, "user32")

using namespace geode::prelude;

namespace {
    template <class T>
    T* scanForType(CCNode* node) {
        if (auto found = typeinfo_cast<T*>(node)) return found;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren()))
            if (auto found = scanForType<T>(child)) return found;
        return nullptr;
    }

    PlayLayer* findActivePlayLayer() {
        if (auto scene = CCDirector::get()->getRunningScene())
            return scanForType<PlayLayer>(scene);
        return nullptr;
    }

    bool pauseLayerIsOpen() {
        if (auto scene = CCDirector::get()->getRunningScene())
            return scanForType<PauseLayer>(scene) != nullptr;
        return false;
    }

    bool gameIsForeground() {
        HWND fore = GetForegroundWindow();
        if (!fore) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fore, &pid);
        return pid == GetCurrentProcessId();
    }

    void tryPause() {
        if (!gameIsForeground()) {
            log::info("side button: game is not the foreground window - ignoring");
            return;
        }
        auto pl = findActivePlayLayer();
        if (!pl) {
            log::info("side button: not inside a level - ignoring");
            return;
        }
        bool pausedFlag = pl->m_isPaused;
        bool openLayer = pauseLayerIsOpen();
        bool notReady = !pl->m_started || pl->m_loadingLayer != nullptr;
        log::info(
            "side button: inLevel=1 isPaused={} pauseOpen={} started={} loadingLayer={}",
            pausedFlag ? 1 : 0,
            openLayer ? 1 : 0,
            pl->m_started ? 1 : 0,
            (pl->m_loadingLayer != nullptr) ? 1 : 0
        );
        if (pausedFlag || openLayer || notReady) return;
        pl->pauseGame(false);
        log::info("side button: pauseGame(false) called, m_isPaused now={}", pl->m_isPaused ? 1 : 0);
    }
}

$on_mod(Loaded) {
    MouseInputEvent().listen([](MouseInputData& data) {
        using Action = MouseInputData::Action;
        using Button = MouseInputData::Button;

        if (data.action != Action::Press) return ListenerResult::Propagate;
        if (data.button != Button::Button4 && data.button != Button::Button5)
            return ListenerResult::Propagate;

        // Run the decision on the next scheduler tick instead of inside the
        // raw-input WndProc chain, so pauseGame never runs mid-frame.
        queueInMainThread([] { tryPause(); });
        return ListenerResult::Propagate;
    }).leak();

    log::info("Side Button Pause v1.0.3 loaded: Mouse 4 / Mouse 5 pause the level");
}
