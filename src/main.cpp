// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// v1.0.5 - the field-read gates are gone. Runtime evidence (2026-10-21,
// log Geode 22.35) showed that in GD 2.2091 the pinned bindings' field
// layout for GJBaseGameLayer does not behave as the dataset suggests:
// GJBaseGameLayer::m_started stayed 0 and m_loadingLayer stayed non-null
// while the cube WAS moving, so pauseGame was never reachable. Conclusion:
// do NOT gate on any GJBaseGameLayer/PlayLayer field. All state is now
// derived from the running scene with typeinfo_cast (the technique that
// is proven to work in this game):
//  - PlayLayer on the scene  -> we are inside a level,
//  - PauseLayer on the scene -> pause already open (also the guard that
//    prevents the double-pauseGame crash),
//  - a visible GJGameLoadingLayer on the scene -> level still loading.
// Calling pauseGame(false) outside these states is vanilla-equivalent to
// the game's own auto-pause on focus loss, which can happen at any moment
// inside a level - including the death animation and the level end walk,
// where both are known-good vanilla paths.
// The suspect fields are still READ and LOGGED for diagnostics only,
// every gate decision is logged for off-board analysis via geode logs.
//
// v1.0.4 - extra "gameplay is actually running" gates on
// m_player1->m_isDead / m_levelEndAnimationStarted / m_hasCompletedLevel.
// v1.0.3 - loader MouseInputEvent + queueInMainThread + typeinfo scene scan
// (no WndProc, no RegisterRawInputDevices, no "= inline;" stub callers).

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/GJGameLoadingLayer.hpp>
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

    template <class T>
    bool layerIsInScene() {
        if (auto scene = CCDirector::get()->getRunningScene()) {
            auto* layer = scanForType<T>(scene);
            return layer != nullptr && layer->isVisible();
        }
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
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        auto pl = scanForType<PlayLayer>(scene);
        if (!pl) {
            log::info("side button: not inside a level - ignoring");
            return;
        }
        bool pauseOpen = layerIsInScene<PauseLayer>();
        bool loadingOpen = layerIsInScene<GJGameLoadingLayer>();
        log::info(
            "side button: inLevel=1 pauseOnScene={} loadingOnScene={} | fields(m_isPaused={} m_started={} m_loadingField={})",
            pauseOpen ? 1 : 0,
            loadingOpen ? 1 : 0,
            pl->m_isPaused ? 1 : 0,
            pl->m_started ? 1 : 0,
            (pl->m_loadingLayer != nullptr) ? 1 : 0
        );
        if (pauseOpen || loadingOpen) return;
        pl->pauseGame(false);
        auto after = CCDirector::get()->getRunningScene();
        bool pauseNow = after != nullptr && scanForType<PauseLayer>(after) != nullptr;
        log::info(
            "side button: pauseGame(false) called -> m_isPaused={} pauseOnScene={} (field reads can be wrong on 2.2091)",
            pl->m_isPaused ? 1 : 0,
            pauseNow ? 1 : 0
        );
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
        // loader's input pipeline, so pauseGame never runs mid-frame.
        queueInMainThread([] { tryPause(); });
        return ListenerResult::Propagate;
    }).leak();

    log::info("Side Button Pause v1.0.5 loaded: Mouse 4 / Mouse 5 pause the level");
}
