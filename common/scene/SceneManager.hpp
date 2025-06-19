#pragma once

#include <filesystem>
#include <optional>
#include <cinttypes>
#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/BlockingTransferHelper.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <etna/VertexInput.hpp>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>

struct Material
{
  uint32_t albedoIndex = static_cast<const uint32_t>(-1);
  glm::vec4 albedo = {1.0, 1.0, 1.0, 1.0};
  uint32_t normalIndex = static_cast<const uint32_t>(-1);
  float normal = 1.0;
};

struct Element
{
  std::uint32_t vertexOffset;
  std::uint32_t vertexCount;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  Material material = {};
};

struct PositionedElement
{
  Element element;
  std::uint32_t matrixPos;
};

struct Vertex
{
  glm::vec4 positionAndNormal;
  glm::vec4 texCoordAndTangentAndPadding;
};

struct Texture
{
  etna::Image image;
};

struct SceneData
{
  etna::Buffer vertexData;
  etna::Buffer indexData;
  std::vector<etna::Image> textures;
  std::vector<PositionedElement> p_elements;
  std::vector<glm::mat4> transforms;
  etna::VertexByteStreamFormatDescription vertexDesc;
};

class SceneManager
{
public:
  std::optional<SceneData> selectScene(std::filesystem::path path);

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<std::uint32_t> meshes;
  };

  ProcessedInstances processInstances(const tinygltf::Model& model) const;

  struct Mesh
  {
    std::uint32_t firstRelem;
    std::uint32_t relemCount;
  };

  struct ProcessedMeshes
  {
    std::vector<Element> relems;
    std::vector<Mesh> meshes;
  };

  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;

  std::vector<PositionedElement> positioned_elements;

  std::vector<PositionedElement> processGroups(
    const std::vector<uint32_t>& instMeshes,
    const std::vector<Mesh>& meshes,
    const std::vector<Element>& relems);

  struct GpuData
  {
    etna::Buffer vertexData;
    etna::Buffer indexData;
    std::vector<etna::Image> textures;
  };
  GpuData uploadGpuData(const tinygltf::Model& model) const;

  etna::VertexByteStreamFormatDescription getVertexFormatDescription() const;


private:
  tinygltf::TinyGLTF loader;
};
