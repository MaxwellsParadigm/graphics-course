#include "WorldRenderer.hpp"
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/ext.hpp>
#include <imgui.h>

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();
  defaultSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "default_sampler"});

  gBuffer.color = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "gBuffer_color",
    .format = vk::Format::eA8B8G8R8SrgbPack32,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
    .flags = vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
  });

  gBuffer.normal = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "gBuffer_normal",
    .format = vk::Format::eA8B8G8R8SnormPack32,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage});

  gBuffer.depth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "gBuffer_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage =
      vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
  });
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  Renderer.loadScene(path);
}

void WorldRenderer::loadShaders()
{
  Renderer.loadShaders();
  etna::create_program(
    "fxaa",
    {BINDLESS_SHADERS_ROOT "fxaa.frag.spv",
     BINDLESS_SHADERS_ROOT "fxaa.vert.spv"});
}


void WorldRenderer::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();
  Renderer.setupPipelines();

  fxaaPipeline = pipelineManager.createGraphicsPipeline(
    "fxaa",
    etna::GraphicsPipeline::CreateInfo{
      .depthConfig =
        {
          .depthTestEnable = vk::False,
          .depthWriteEnable = vk::False,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats =
            {
              vk::Format::eB8G8R8A8Srgb,
            },
        },
    });
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
    eye = packet.mainCam.position;
  }
  Renderer.update(worldViewProj);
}

void WorldRenderer::drawGui()
{
  ImGui::Begin("IMGUI");
  ImGui::Checkbox("FXAA", &FXAA_ON);
  ImGui::End();
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);
  Renderer.prepareForRender(cmd_buf);
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    etna::set_state(
      cmd_buf,
      gBuffer.color.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      gBuffer.normal.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      gBuffer.depth.get(),
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
        vk::PipelineStageFlagBits2::eLateFragmentTests,
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
      vk::ImageLayout::eDepthAttachmentOptimal,
      vk::ImageAspectFlagBits::eDepth);

    etna::flush_barriers(cmd_buf);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = gBuffer.color.get(),
        .view = gBuffer.color.getView({.usageFlags = vk::ImageUsageFlagBits::eColorAttachment})},
       {.image = gBuffer.normal.get(), .view = gBuffer.normal.getView({})}},
      {.image = gBuffer.depth.get(), .view = gBuffer.depth.getView({})});

    Renderer.renderScene(cmd_buf);
  }
  {
    etna::set_state(
      cmd_buf,
      gBuffer.color.get(),
      vk::PipelineStageFlagBits2::eBlit,
      vk::AccessFlagBits2::eTransferRead,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      target_image,
      vk::PipelineStageFlagBits2::eBlit,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    std::array offsets = {
      vk::Offset3D{},
      vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1}};

    auto imageBlit = vk::ImageBlit{
      .srcSubresource =
        vk::ImageSubresourceLayers{
          .aspectMask = vk::ImageAspectFlagBits::eColor,
          .mipLevel = 0,
          .baseArrayLayer = 0,
          .layerCount = 1},
      .srcOffsets = offsets,
      .dstSubresource =
        vk::ImageSubresourceLayers{
          .aspectMask = vk::ImageAspectFlagBits::eColor,
          .mipLevel = 0,
          .baseArrayLayer = 0,
          .layerCount = 1},
      .dstOffsets = offsets};

    cmd_buf.blitImage(
      gBuffer.color.get(),
      vk::ImageLayout::eTransferSrcOptimal,
      target_image,
      vk::ImageLayout::eTransferDstOptimal,
      1,
      &imageBlit,
      vk::Filter::eLinear);
  }
}

void WorldRenderer::renderWorldFXAA(vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);
  Renderer.prepareForRender(cmd_buf);
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    etna::set_state(
      cmd_buf,
      gBuffer.color.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      gBuffer.normal.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      gBuffer.depth.get(),
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
        vk::PipelineStageFlagBits2::eLateFragmentTests,
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
      vk::ImageLayout::eDepthAttachmentOptimal,
      vk::ImageAspectFlagBits::eDepth);

    etna::flush_barriers(cmd_buf);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = gBuffer.color.get(),
        .view = gBuffer.color.getView({.usageFlags = vk::ImageUsageFlagBits::eColorAttachment})},
       {.image = gBuffer.normal.get(), .view = gBuffer.normal.getView({})}},
      {.image = gBuffer.depth.get(), .view = gBuffer.depth.getView({})});

    Renderer.renderScene(cmd_buf);
  }
  {
    ETNA_PROFILE_GPU(cmd_buf, fxaa);

    etna::set_state(
      cmd_buf,
      gBuffer.color.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      target_image,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {
        {.image = target_image,
         .view = target_image_view,
         .loadOp = vk::AttachmentLoadOp::eDontCare},
      },
      {});


    auto info = etna::get_shader_program("fxaa");

    auto bind0 = gBuffer.color.genBinding(
      defaultSampler.get(),
      vk::ImageLayout::eShaderReadOnlyOptimal,
      {.format = vk::Format::eA8B8G8R8UnormPack32});

    auto descSet = etna::create_descriptor_set(
      info.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, bind0}});
    auto vkSet = descSet.getVkSet();
    auto layout = fxaaPipeline.getVkPipelineLayout();

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, fxaaPipeline.getVkPipeline());
    cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, 1, &vkSet, 0, nullptr);

    fxaaPushConstants.rcpFrame = 1.0f / glm::vec2(resolution);
    cmd_buf.pushConstants<decltype(fxaaPushConstants)>(
      layout, vk::ShaderStageFlagBits::eFragment, 0, fxaaPushConstants);

    cmd_buf.draw(3, 1, 0, 0);
  }
}
