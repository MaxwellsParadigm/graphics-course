#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <chrono>


#include "etna/GraphicsPipeline.hpp"
#include "wsi/OsWindowingManager.hpp"


class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  etna::Image generatedTextureImage;
  etna::Image loadedTextureImage1;

  etna::Sampler defaultSampler;

  etna::GraphicsPipeline graphicsPipeline;
  etna::GraphicsPipeline texturePipeline;

  std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();
  glm::vec2 mouse_pos;

  bool is_textures_loaded = false;
};