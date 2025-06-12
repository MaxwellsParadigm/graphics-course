#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Etna.hpp>
#include <etna/Buffer.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/VertexInput.hpp>

struct Material {
  enum class ImageId : uint32_t {
    Invalid = ~uint32_t{0}
  };

  ImageId albedoId = ImageId::Invalid;

  static Material none() {
    return Material{ImageId::Invalid};
  }
};

struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  Material material;
};

struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

class SceneManager
{
public:
  SceneManager();
  void selectScene(std::filesystem::path path);
  void selectSceneCompressed(std::filesystem::path path);
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const std::uint32_t> getInstanceMeshes() { return instanceMeshes; }
  std::span<const Mesh> getMeshes() { return meshes; }
  std::span<etna::Image> getImages() { return images; }
  std::span<const RenderElement> getRenderElements() { return renderElements; }
  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }
  etna::VertexByteStreamFormatDescription getVertexFormatDescription();
private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);
  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<std::uint32_t> meshes;
  };
  ProcessedInstances processInstances(const tinygltf::Model& model) const;
  struct Vertex
  {
    glm::vec4 positionAndNormal;
    glm::vec4 texCoordAndTangentAndPadding;
  };
  static_assert(sizeof(Vertex) == sizeof(float) * 8);
  struct ProcessedMeshes
  {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<tinygltf::Image> images;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
  };
  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;
  ProcessedMeshes processMeshesCompressed(const tinygltf::Model& model) const;
  void uploadData(std::span<const Vertex> vertices, std::span<const std::uint32_t>,
                  std::span<const tinygltf::Image> imges = std::span<const tinygltf::Image>());
private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;
  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<std::uint32_t> instanceMeshes;
  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;
  std::vector<etna::Image> images;
};
