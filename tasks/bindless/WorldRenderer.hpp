#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>
#include "Bindless.hpp"
#include "wsi/Keyboard.hpp"
#include "FramePacket.hpp"

class WorldRenderer
{
public:
  WorldRenderer() = default;

  void loadScene(std::filesystem::path path);
  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines();
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image);
  void renderWorldFXAA(vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);
  bool FXAA_ON = true;

private:
  Bindless Renderer;
  etna::Sampler defaultSampler;
  etna::GraphicsPipeline fxaaPipeline{};
  glm::mat4x4 worldViewProj;
  glm::vec3 eye;
  glm::uvec2 resolution;

  struct
  {
    etna::Image normal;
    etna::Image color;
    etna::Image depth;
  } gBuffer;

  struct
  {
    glm::vec2 rcpFrame;
    float subpix = 7.0f;
    float edgeThreshold = 1.0f;
    float edgeThresholdMin = 1.0f;
  } fxaaPushConstants;
};
