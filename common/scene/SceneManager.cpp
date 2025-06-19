#include "SceneManager.hpp"
#include <limits>
#include <stack>
#include <spdlog/spdlog.h>
#include <fmt/std.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <iostream>

std::optional<tinygltf::Model> SceneManager::loadModel(std::filesystem::path path)
{
  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension: '{}'. Expected .gltf or .glb.", ext);
    return std::nullopt;
  }

  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
    return std::nullopt;
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");

  return model;
}

std::optional<SceneData> SceneManager::selectScene(std::filesystem::path path)
{
  auto maybeModel = loadModel(path);

  auto model = std::move(*maybeModel);

  auto [transforms, instMeshes] = processInstances(model);

  auto [relems, meshs] = processMeshes(model);

  auto elems = processGroups(instMeshes, meshs, relems);
  auto vertexDesc = getVertexFormatDescription();

  auto [vertexData, indexData, textures] = uploadGpuData(model);

  return SceneData{
    .vertexData = std::move(vertexData),
    .indexData = std::move(indexData),
    .textures = std::move(textures),
    .p_elements = std::move(elems),
    .transforms = std::move(transforms),
    .vertexDesc = std::move(vertexDesc),
  };
}

SceneManager::ProcessedInstances SceneManager::processInstances(const tinygltf::Model& model) const
{
  std::vector nodeTransforms(model.nodes.size(), glm::identity<glm::mat4x4>());

  for (std::size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
  {
    const auto& node = model.nodes[nodeIdx];
    auto& transform = nodeTransforms[nodeIdx];
    if (!node.matrix.empty())
    {
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          transform[i][j] = static_cast<float>(node.matrix[4 * i + j]);
    }
  }
  std::stack<std::size_t> vertices;
  for (auto vert : model.scenes[model.defaultScene].nodes)
    vertices.push(vert);

  while (!vertices.empty())
  {
    auto vert = vertices.top();
    vertices.pop();

    for (auto child : model.nodes[vert].children)
    {
      nodeTransforms[child] = nodeTransforms[vert] * nodeTransforms[child];
      vertices.push(child);
    }
  }

  ProcessedInstances result;

  {
    std::size_t totalNodesWithMeshes = 0;
    for (const auto& node : model.nodes)
      if (node.mesh >= 0)
      {
        ++totalNodesWithMeshes;
      }
    result.matrices.reserve(totalNodesWithMeshes);
    result.meshes.reserve(totalNodesWithMeshes);
  }

  for (std::size_t i = 0; i < model.nodes.size(); ++i)
    if (model.nodes[i].mesh >= 0)
    {
      result.matrices.push_back(nodeTransforms[i]);
      result.meshes.push_back(model.nodes[i].mesh);
    }

  return result;
}

static Material getMaterial(const tinygltf::Model& model, int index)
{
  glm::vec4 albedo;
  for (uint32_t i = 0; i < 4; ++i)
  {
    albedo[i] = static_cast<float>(model.materials[index].pbrMetallicRoughness.baseColorFactor[i]);
  }
  return {
    .albedoIndex =
      static_cast<uint32_t>(model.materials[index].pbrMetallicRoughness.baseColorTexture.index),
    .albedo = albedo,
    .normalIndex = static_cast<uint32_t>(model.materials[index].normalTexture.index),
    .normal = static_cast<float>(model.materials[index].normalTexture.scale),
  };
}

SceneManager::ProcessedMeshes SceneManager::processMeshes(const tinygltf::Model& model) const
{
  ProcessedMeshes res;
  for (const auto& mesh : model.meshes)
  {
    {
      Mesh current = Mesh{
        .firstRelem = static_cast<std::uint32_t>(res.relems.size()),
        .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
      };
      res.meshes.push_back(current);
    }

    for (const auto& prim : mesh.primitives)
    {
      auto& indAccessor = model.accessors[prim.indices];
      auto& posAccessor = model.accessors[prim.attributes.at("POSITION")];

      res.relems.push_back(Element{
        .vertexOffset = static_cast<std::uint32_t>(posAccessor.byteOffset / sizeof(Vertex)),
        .vertexCount = static_cast<std::uint32_t>(posAccessor.count),
        .indexOffset = static_cast<std::uint32_t>(indAccessor.byteOffset / sizeof(uint32_t)),
        .indexCount = static_cast<std::uint32_t>(indAccessor.count),
        .material = getMaterial(model, prim.material),
      });
    }
  }

  return res;
}

std::vector<PositionedElement> SceneManager::processGroups(
  const std::vector<uint32_t>& instMeshes,
  const std::vector<Mesh>& meshes,
  const std::vector<Element>& relems)
{

  auto forEachRelem = [&](const auto& func) {
    for (size_t i = 0; i < instMeshes.size(); ++i)
    {
      size_t meshInd = instMeshes[i];
      auto& currMesh = meshes[meshInd];
      for (size_t relemInd = 0; relemInd < currMesh.relemCount; ++relemInd)
      {
        func(relemInd + currMesh.firstRelem, i);
      }
    }
  };

  std::vector<size_t> relemUseCounts(relems.size(), 0);
  forEachRelem([&](size_t relemInd, size_t) { relemUseCounts[relemInd] += 1; });

  size_t singleRelemCount = 0;
  for (auto curr : relemUseCounts)
  {
    if (curr == 1)
    {
      ++singleRelemCount;
    }
  }
  std::vector<bool> isSingle(relems.size());
  std::vector<PositionedElement> singleRelems;
  singleRelems.reserve(singleRelemCount);
  forEachRelem([&](size_t relemInd, size_t meshInd) {
    if (relemUseCounts[relemInd] == 1)
    {
      isSingle[relemInd] = true;
      relemUseCounts[relemInd] = 0;
      PositionedElement curr = {
        .element = relems[relemInd],
        .matrixPos = static_cast<uint32_t>(meshInd),
      };
      singleRelems.push_back(curr);
    }
  });

  return singleRelems;
}

etna::VertexByteStreamFormatDescription SceneManager::getVertexFormatDescription() const
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
      },
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = sizeof(glm::vec4),
      },
    }};
}

SceneManager::GpuData SceneManager::uploadGpuData(const tinygltf::Model& model) const
{
  auto& ctx = etna::get_context();
  auto mgr = ctx.createOneShotCmdMgr();
  auto transfer = etna::BlockingTransferHelper({.stagingSize = 1024 * 1024});

  const std::byte* data = reinterpret_cast<const std::byte*>(model.buffers[0].data.data());
  auto vertexDataCpu = std::span{data, model.bufferViews[0].byteLength};

  etna::Buffer vertexData = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = model.bufferViews[0].byteLength,
    .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "SM_VertexData"});

  transfer.uploadBuffer(*mgr, vertexData, 0, vertexDataCpu);

  auto indexDataCpu =
    std::span{data + model.bufferViews[0].byteLength, model.bufferViews[1].byteLength};

  etna::Buffer indexData = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = model.bufferViews[0].byteLength,
    .bufferUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "SM_IndexData"});

  transfer.uploadBuffer(*mgr, indexData, 0, indexDataCpu);

  std::vector<etna::Image> textures;

  for (auto& image : model.images)
  {
    textures.emplace_back(ctx.createImage(etna::Image::CreateInfo{
      .extent =
        vk::Extent3D{
          .width = static_cast<uint32_t>(image.width),
          .height = static_cast<uint32_t>(image.height),
          .depth = 1},
      .name = "texture",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    }));
    const std::byte* imageData = reinterpret_cast<const std::byte*>(image.image.data());
    transfer.uploadImage(*mgr, textures.back(), 0, 0, {imageData, image.image.size()});
  }
  return {std::move(vertexData), std::move(indexData), std::move(textures)};
}
