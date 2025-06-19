#include <etna/BlockingTransferHelper.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Assert.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Sampler.hpp>
#include <etna/Profiling.hpp>
#include "scene/SceneManager.hpp"
#include "shaders/bind.h"
#include "Bindless.hpp"

static Compact makeCompat(const Material& src)
{
  return {
    .albedoIndex = src.albedoIndex,
    .normalIndex = src.normalIndex,
    .normal = src.normal,
    .albedo = src.albedo,
  };
}

void Bindless::loadScene(std::filesystem::path path)
{
  ZoneScoped;
  SceneData sceneDesc;
  {
    ZoneScoped;
    SceneManager loader;
    auto maybeSceneDesc = loader.selectScene(path);
    sceneDesc = std::move(*maybeSceneDesc);
  }
  {
    ZoneScoped;
    etna::BlockingTransferHelper transfer(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = 1024 * 1024,
    });
    std::unique_ptr<etna::OneShotCmdMgr> mgr = etna::get_context().createOneShotCmdMgr();

    elementsCount = sceneDesc.p_elements.size();

    vertexDesc = sceneDesc.vertexDesc;

    auto& ctx = etna::get_context();

    MtWTransforms = ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sceneDesc.transforms.size() * sizeof(glm::mat4),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "Matrices",
    });
    transfer.uploadBuffer(
      *mgr, MtWTransforms, 0, std::span<const glm::mat4>(sceneDesc.transforms));
    
      std::vector<Indirect> tmp(sceneDesc.p_elements.size());
    elementsData = ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = sceneDesc.p_elements.size() * sizeof(Indirect),
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "Drawcalls",
      });

      for (size_t i = 0; i < sceneDesc.p_elements.size(); ++i)
      {
        auto& src = sceneDesc.p_elements[i];
        auto& dst = tmp[i];
        dst.command = {
          .indexCount = src.element.indexCount,
          .instanceCount = 0,
          .firstIndex = src.element.indexOffset,
          .vertexOffset = static_cast<int32_t>(src.element.vertexOffset),
          .firstInstance = static_cast<uint32_t>(i),
        };
        dst.matrWfMIndex = src.matrixPos;
        dst.material = makeCompat(src.element.material);
      }
      transfer.uploadBuffer(*mgr, elementsData, 0, std::span<const Indirect>(tmp));
    vertexData = std::move(sceneDesc.vertexData);
    indexData = std::move(sceneDesc.indexData);
    textures = std::move(sceneDesc.textures);
  }
  createDescSet();
}

void Bindless::loadShaders()
{
  ZoneScoped;
  etna::create_program(
    "render",
    {BINDLESS_SHADERS_ROOT "render.frag.spv",
     BINDLESS_SHADERS_ROOT "render.vert.spv"});
  etna::create_program(
    "collect",
    {BINDLESS_SHADERS_ROOT "collect.comp.spv"});
}

void Bindless::setupPipelines()
{
  ZoneScoped;
  auto& pipelineManager = etna::get_context().getPipelineManager();
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = vertexDesc,
    }},
  };
  auto device = etna::get_context().getDevice();
  {
    std::array<vk::DescriptorSetLayout, 2> descSetLayouts;
    auto info = etna::get_shader_program("render");
    descSetLayouts[0] = info.getDescriptorSetLayout(0);
    descSetLayouts[1] = textureSetLayout;
    auto pushConstRange = info.getPushConst();
    RenderLayout =
      etna::unwrap_vk_result(device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 2,
        .pSetLayouts = descSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstRange,
      }));
    RenderPipeline = pipelineManager.createGraphicsPipeline(
      "render",
      RenderLayout,
      etna::GraphicsPipeline::CreateInfo{
        .vertexShaderInput = sceneVertexInputDesc,
        .rasterizationConfig =
          vk::PipelineRasterizationStateCreateInfo{
            .polygonMode = vk::PolygonMode::eFill,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.f,
          },
        .blendingConfig =
          {.attachments =
             {vk::PipelineColorBlendAttachmentState{
                .blendEnable = vk::False,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
              },
              vk::PipelineColorBlendAttachmentState{
                .blendEnable = vk::False,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
              }},
           .logicOp = vk::LogicOp::eSet},
        .fragmentShaderOutput =
          {
            .colorAttachmentFormats =
              {vk::Format::eB10G11R11UfloatPack32, vk::Format::eA8B8G8R8SnormPack32},
            .depthAttachmentFormat = vk::Format::eD32Sfloat,
          },
      });
  }
  CollectPipeline = pipelineManager.createComputePipeline("collect", {});
}

void Bindless::renderScene(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, renderStatic);

    auto info = etna::get_shader_program("render");
    auto binding0 = elementsData.genBinding();
    auto binding1 = MtWTransforms.genBinding();

    auto set = etna::create_descriptor_set(
      info.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, binding0},
        etna::Binding{1, binding1},
      });
    vk::DescriptorSet vkSet = set.getVkSet();
    auto& pipeline = RenderPipeline;
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, RenderLayout, 0, {vkSet, textureSet}, {});
    cmd_buf.bindVertexBuffers(0, {vertexData.get()}, {0});
    cmd_buf.bindIndexBuffer(indexData.get(), 0, vk::IndexType::eUint32);
    cmd_buf.pushConstants<glm::mat4>(
      RenderLayout, vk::ShaderStageFlagBits::eVertex, 0, {matrVfW});
    cmd_buf.drawIndexedIndirect(
      elementsData.get(),
      offsetof(Indirect, command),
      static_cast<uint32_t>(elementsCount),
      sizeof(Indirect));
}

void Bindless::prepareForRender(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, collect);

    etna::set_state(
      cmd_buf,
    elementsData.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead);
    etna::flush_barriers(cmd_buf);

    auto info = etna::get_shader_program("collect");
    auto binding0 = elementsData.genBinding();

    auto set = etna::create_descriptor_set(
      info.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, binding0},
      });
    vk::DescriptorSet vkSet = set.getVkSet();
    auto& pipeline = CollectPipeline;
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getVkPipeline());
    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    cmd_buf.pushConstants<glm::mat4>(
      pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, {matrVfW});
    cmd_buf.dispatch((14 + 31) / 32, 1, 1);
    etna::set_state(
      cmd_buf,
      elementsData.get(),
      vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader,
      vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead);
}

void Bindless::createDescSet()
{

  auto& ctx = etna::get_context();
  auto device = ctx.getDevice();
  {
    vk::DescriptorPoolSize poolSize{
      .type = vk::DescriptorType::eCombinedImageSampler,
      .descriptorCount = MAX_DESCRIPTOR_NUM,
    };

    vk::DescriptorPoolCreateInfo info{
      .flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &poolSize,
    };

    textureSetPool = etna::unwrap_vk_result(device.createDescriptorPool(info));
  }
  {
    auto binding = vk::DescriptorSetLayoutBinding{
      .binding = 0,
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .descriptorCount = MAX_DESCRIPTOR_NUM,
      .stageFlags = vk::ShaderStageFlagBits::eAll,
      .pImmutableSamplers = nullptr,
    };

    auto bindlessFlags = vk::DescriptorBindingFlags{
      vk::DescriptorBindingFlagBits::ePartiallyBound |
      vk::DescriptorBindingFlagBits::eUpdateAfterBind};
    auto extendedInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT{
      .bindingCount = 1,
      .pBindingFlags = &bindlessFlags,
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{
      .pNext = &extendedInfo,
      .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPoolEXT,
      .bindingCount = 1,
      .pBindings = &binding,
    };

    textureSetLayout = etna::unwrap_vk_result(device.createDescriptorSetLayout(layoutInfo));
  }
  {
    uint32_t descriptorCount = static_cast<uint32_t>(MAX_DESCRIPTOR_NUM);

    vk::DescriptorSetVariableDescriptorCountAllocateInfo extraInfo{
      .descriptorSetCount = 1,
      .pDescriptorCounts = &descriptorCount,
    };

    vk::DescriptorSetAllocateInfo setAllocInfo{
      .pNext = &extraInfo,
      .descriptorPool = textureSetPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &textureSetLayout,
    };

    textureSet = etna::unwrap_vk_result(device.allocateDescriptorSets(setAllocInfo))[0];
  }
  {
    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(textures.size());
    for (auto& texture : textures)
    {
      imageInfos.push_back(vk::DescriptorImageInfo{
        .sampler = sampler.get(),
        .imageView =
          texture.getView({0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers, {}, {}}),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      });
    }

    vk::WriteDescriptorSet write{
      .dstSet = textureSet,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorCount = static_cast<uint32_t>(textures.size()),
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .pImageInfo = imageInfos.data(),
    };

    device.updateDescriptorSets(1, &write, 0, nullptr);
  }
}

Bindless::~Bindless()
{
  auto device = etna::get_context().getDevice();
  device.destroyDescriptorPool(textureSetPool);
  device.destroyDescriptorSetLayout(textureSetLayout);
  device.destroyPipelineLayout(RenderLayout);
}
