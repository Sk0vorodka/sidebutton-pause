// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// How it works:
//  * Geode's built-in Raw Input layer (loader/src/platform/windows/input.cpp)
//    captures WM_INPUT events and sends MouseInputEvent for every mouse
//    button, including Button4 (back) and Button5 (forward).
//  * We listen to that event and, when we're inside a PlayLayer and not
//    already paused, call PlayLayer::pauseGame(false).
//  * Hooks on pauseGame / resume / resumeAndRestart / onExit keep track of
//    whether the pause menu is open, so repeated presses don't toggle pause.
//
// The input event is dispatched from the game's window message pump (main
// thread), so calling pauseGame directly from the listener is safe.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/loader/ModEvent.hpp>

using namespace geode::prelude;

namespace {
    // Whether the pause menu is currently open. Only touched on the main
    // thread (window message pump / game loop), so a plain bool is fine.
    bool g_pausedOpen = false;

    void pauseIfInLevel() {
        auto pl = PlayLayer::get();
        if (!pl || g_pausedOpen) return;
        pl->pauseGame(false);
    }
}

// Track pause state so that the second side button press while paused (and
// presses arriving from the pause menu itself) do not toggle anything.
class $modify(PlayLayer) {
    void pauseGame(bool unfocused) {
        g_pausedOpen = true;
        PlayLayer::pauseGame(unfocused);
    }

    void resume() {
        g_pausedOpen = false;
        PlayLayer::resume();
    }

    void resumeAndRestart(bool fromStart) {
        g_pausedOpen = false;
        PlayLayer::resumeAndRestart(fromStart);
    }

    void onExit() {
        g_pausedOpen = false;
        PlayLayer::onExit();
    }
};

$on_mod(Loaded) {
    // Leak the listener handle so the receiver lives for the whole session.
    MouseInputEvent().listen([](MouseInputData& data) {
        using Action = MouseInputData::Action;
        using Button = MouseInputData::Button;

        if (data.action != Action::Press) return ListenerResult::Propagate;
        if (data.button != Button::Button4 && data.button != Button::Button5)
            return ListenerResult::Propagate;

        pauseIfInLevel();
        return ListenerResult::Propagate;
    }).leak();

    log::info("Side Button Pause loaded: Mouse 4 / Mouse 5 pause the level");
}
