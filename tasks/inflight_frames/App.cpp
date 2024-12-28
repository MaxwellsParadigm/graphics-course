#include "App.hpp"
#include "etna/Buffer.hpp"
#include "etna/DescriptorSet.hpp"
#include "etna/Window.hpp"
#include <cstdint>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <memory>
#include <stb_image.h>
#include <utility>
#include <vulkan/vulkan_enums.hpp>

App::App()
  : resolution{1280, 720}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};
    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a
    // "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = NUM_FRAMES_IN_FLIGHT,
    });
  }

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = false,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();

  Texture = etna::get_context().createImage(
    {.extent = vk::Extent3D{resolution.x, resolution.y, 1},
     .name = "texture",
     .format = vk::Format::eB8G8R8A8Srgb,
     .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});


  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "default_sampler"});

  etna::create_program(
    "inflight_frames",
    {INFLIGHT_FRAMES_SHADERS_ROOT "toy.frag.spv", INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});
  graphicsPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "inflight_frames",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}}});

  etna::create_program("texture", {INFLIGHT_FRAMES_SHADERS_ROOT "texture.frag.spv", INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});
  texturePipeline = etna::get_context().getPipelineManager().createGraphicsPipeline("texture", etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}}
  });

  for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
  {
    FrameBuffer[i] = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(Params),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
      .name = "buffer"
     });
  }

  {
    int height, width, channels;
    unsigned char* textureData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &width, &height, &channels, 4);
    assert(textureData != nullptr);

    Image1 = etna::get_context().createImage({
      .extent = vk::Extent3D{(uint32_t)width, (uint32_t)height, 1},
      .name = "loaded_texture",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    });

    etna::BlockingTransferHelper transferHelper({.stagingSize = VkDeviceSize(width * height * 4)});

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = etna::get_context().createOneShotCmdMgr();

    transferHelper.uploadImage(
      *oneShotCmdMgr,
      Image1,
      0,
      0,
      std::span<std::byte>(reinterpret_cast<std::byte*>(textureData), width * height * 4));

    stbi_image_free(textureData);
  }

  {
    int height, width, channels;
    unsigned char* textureData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/texture1.bmp", &width, &height, &channels, 4);
    assert(textureData != nullptr);

    stbi_image_free(textureData);
  }
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    ZoneScoped;

    {
      ZoneScoped;
      windowing.poll();
    }

    drawFrame();

    FrameMark;
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      ETNA_PROFILE_GPU(currentCmdBuf, "frame_render");

      {
        ZoneScoped;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }

      etna::set_state(
        currentCmdBuf,
        Texture.get(),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      if (!is_textures_loaded)
      {
        etna::set_state(
          currentCmdBuf,
          Texture.get(),
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::AccessFlagBits2::eColorAttachmentWrite,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);

        {
          etna::RenderTargetState state{
            currentCmdBuf,
            {{}, {resolution.x, resolution.y}},
            {{Texture.get(), Texture.getView({})}},
            {}};

          currentCmdBuf.bindPipeline(
            vk::PipelineBindPoint::eGraphics, texturePipeline.getVkPipeline());

          currentCmdBuf.pushConstants(
            texturePipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(resolution),
            &resolution);

          currentCmdBuf.draw(3, 1, 0, 0);
        }
        is_textures_loaded = true;
      }

      etna::set_state(
        currentCmdBuf,
        Texture.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      {
        etna::RenderTargetState state{
          currentCmdBuf, {{}, {resolution.x, resolution.y}}, {{backbuffer, backbufferView}}, {}};

        auto shaderInfo = etna::get_shader_program("inflight_frames");

        auto set = etna::create_descriptor_set(
          shaderInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
            etna::Binding{0, Texture.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{1, Image1.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{2, FrameBuffer[frame_num].genBinding()},
          });

        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, graphicsPipeline.getVkPipeline());

        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          graphicsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});


        Params params{
          .time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start_time).count()/1000.f,
          .resolution = resolution,
          .mouse_pos = osWindow.get()->mouse.freePos
        };

        std::byte* constantsData = FrameBuffer[frame_num].map();
        std::memcpy(constantsData, &params, sizeof(params));
        FrameBuffer[frame_num].unmap();
        if (frame_num == 0)
          frame_num = 1;
        else
          frame_num = 0;

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      
      // At the end of "rendering", we are required to change how the pixels of the
      // swpchain image are laid out in memory to something that is appropriate
      // for presenting to the window (while preserving the content of the pixels!).
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // This looks weird, but is correct. Ask about it later.
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      // And of course flush the layout transition.
      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }

    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be executed by the
    // GPU. Note that the GPU won't start executing our commands before the semaphore is
    // signalled, which will happen when the OS says that the next swapchain image is
    // ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells the OS
    // that it is done executing the command buffer via the renderingDone semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();


  // After a window us un-minimized, we need to restore the swapchain to continue
  // rendering.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = false,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
