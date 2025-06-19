#include "cpp_glsl_compat.h"

struct Command{
  shader_uint indexCount;
  shader_uint instanceCount;
  shader_uint firstIndex;
  shader_int vertexOffset;
  shader_uint firstInstance;
};

struct Compact{
  shader_uint albedoIndex;
  shader_uint normalIndex;
  shader_float normal;
  CPU_ONLY(float padding = -1.0;)

  shader_vec4 albedo;
};

struct Indirect{
  shader_vec3 min_coord;
  shader_uint matrWfMIndex;
  shader_vec3 max_coord;

  Command command;
  Compact material;
};

struct REInstanceRenderInfo
{
  shader_uint mtwTransformIndex;
};
