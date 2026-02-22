/**
 * @file ActionDispatcher.cpp
 * @brief Unified action dispatcher implementation
 */

#include "ActionDispatcher.h"
#include <algorithm>

namespace directpipe {

void ActionDispatcher::dispatch(const ActionEvent& event)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    for (auto* listener : listeners_) {
        listener->onAction(event);
    }
}

void ActionDispatcher::addListener(ActionListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.push_back(listener);
}

void ActionDispatcher::removeListener(ActionListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

void ActionDispatcher::pluginBypass(int pluginIndex)
{
    dispatch({Action::PluginBypass, pluginIndex, 0.0f, ""});
}

void ActionDispatcher::masterBypass()
{
    dispatch({Action::MasterBypass, 0, 0.0f, ""});
}

void ActionDispatcher::setVolume(const std::string& target, float value)
{
    dispatch({Action::SetVolume, 0, value, target});
}

void ActionDispatcher::toggleMute(const std::string& target)
{
    dispatch({Action::ToggleMute, 0, 0.0f, target});
}

void ActionDispatcher::loadPreset(int presetIndex)
{
    dispatch({Action::LoadPreset, presetIndex, 0.0f, ""});
}

void ActionDispatcher::panicMute()
{
    dispatch({Action::PanicMute, 0, 0.0f, ""});
}

void ActionDispatcher::inputGainAdjust(float deltaDq)
{
    dispatch({Action::InputGainAdjust, 0, deltaDq, ""});
}

void ActionDispatcher::inputMuteToggle()
{
    dispatch({Action::InputMuteToggle, 0, 0.0f, ""});
}

} // namespace directpipe
