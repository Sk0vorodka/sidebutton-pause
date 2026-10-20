// artem.sidebutton-pause
// Pausing the level with both side mouse buttons (Mouse 4 / Mouse 5).
//
// How it works:
//  * Geode's built-in Raw Input layer (loader/src/platform/windows/input.cpp)
//    captures WM_INPUT events and sends MouseInputEvent for every mouse
//    button, including Button4 (back) and Button5 (forward).
//  * We listen to that event and, when we're inside a PlayLayer that is not
//    already paused (PlayLayer::m_isPaused), call PlayLayer::pauseGame(false).
//    m_isPaused comes straight from the 2.2091 bindings dataset, so no hooks
//    are needed to track pause state at all.
//
// The input event is dispatched from the game's window message pump (main
// thread), so calling pauseGame directly from the listener is safe.

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/loader/ModEvent.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    // Leak the listener handle so the receiver lives for the whole session.
    MouseInputEvent().listen([](MouseInputData& data) {
        using Action = MouseInputData::Action;
        using Button = MouseInputData::Button;

        if (data.action != Action::Press) return ListenerResult::Propagate;
        if (data.button != Button::Button4 && data.button != Button::Button5)
            return ListenerResult::Propagate;

        if (auto pl = PlayLayer::get()) {
            if (!pl->m_isPaused) {
                pl->pauseGame(false);
            }
        }
        return ListenerResult::Propagate;
    }).leak();

    log::info("Side Button Pause loaded: Mouse 4 / Mouse 5 pause the level");
}
