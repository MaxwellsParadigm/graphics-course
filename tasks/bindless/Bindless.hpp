#include <filesystem>
#include <glm/glm.hpp>
#include <etna/Etna.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/DescriptorSet.hpp>
#include <etna/Sampler.hpp>

class Bindless
{
public:
  ~Bindless();
  void loadScene(std::filesystem::path path);

  void loadShaders();
  void setupPipelines();

  void update(glm::mat4 matrVfW_) { matrVfW = matrVfW_; }

  void prepareForRender(vk::CommandBuffer cmd_buf);
  void renderScene(vk::CommandBuffer cmd_buf);

private:
  void createDescSet();

  etna::Buffer vertexData;
  etna::Buffer indexData;
  etna::Buffer MtWTransforms;
  size_t elementsCount;
  etna::Buffer elementsData;
  etna::ComputePipeline CollectPipeline;
  etna::GraphicsPipeline RenderPipeline;
  vk::PipelineLayout RenderLayout;
  etna::VertexByteStreamFormatDescription vertexDesc;
  std::vector<etna::Image> textures;
  vk::DescriptorSet textureSet;
  vk::DescriptorPool textureSetPool;
  vk::DescriptorSetLayout textureSetLayout;
  glm::mat4 matrVfW{};
  etna::Sampler sampler{etna::Sampler::CreateInfo{.name = "SM_DefaultSampler"}};

  constexpr static size_t MAX_DESCRIPTOR_NUM = 128;
};