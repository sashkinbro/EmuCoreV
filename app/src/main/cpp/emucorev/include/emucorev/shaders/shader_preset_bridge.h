#pragma once

#include <string>

namespace renderer::vulkan {
class ScreenRenderer;
}

namespace emucorev::shaders {

void registerScreenRenderer(renderer::vulkan::ScreenRenderer *renderer);
void unregisterScreenRenderer(renderer::vulkan::ScreenRenderer *renderer);

bool requestPreset(const std::string &pack_root, const std::string &preset_path, std::string &error);
bool setParameter(const std::string &name, float value);
bool resetParameters();
std::string lastPresetError();

} // namespace emucorev::shaders
