#pragma once

#include "etna/Buffer.hpp"
#include "etna/GraphicsPipeline.hpp"
#include "wsi/OsWindowingManager.hpp"
#include <chrono>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/Sampler.hpp>
#include <etna/Window.hpp>

class App
{
public:
  App();
  ~App();

  void run();

  static constexpr size_t NUM_FRAMES_IN_FLIGHT = 2;

private:
  void drawFrame();

private:
  struct Params
  {
    float time;
    glm::vec2 resolution;
    glm::vec2 mouse_pos;
  };

  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;
  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  glm::uvec2 resolution;
  etna::Image Texture;
  etna::Image Image1;

  etna::Sampler defaultSampler;
  etna::GraphicsPipeline graphicsPipeline;
  etna::GraphicsPipeline texturePipeline;
  bool is_textures_loaded = false;
  std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();

  int frame_num = 0;
  std::array<etna::Buffer, 2> FrameBuffer;
};
