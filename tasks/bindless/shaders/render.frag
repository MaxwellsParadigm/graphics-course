#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bind.h"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_tangent;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in flat int in_comm_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(std430, binding = 0) readonly restrict buffer render_buf_0 {
  Indirect comms[];
};

layout(set = 1, binding = 0) uniform sampler2D textures[];

vec4 toLinear(vec4 sRGB)
{
  bvec3 cutoff = lessThan(sRGB.rgb, vec3(0.04045));
  vec3 higher = pow((sRGB.rgb + vec3(0.055))/vec3(1.055), vec3(2.4));
  vec3 lower = sRGB.rgb/vec3(12.92);

  return vec4(mix(higher, lower, cutoff), sRGB.a);
}

void main()
{
  Compact material = comms[in_comm_pos].material;
  out_color = material.albedo;
  if (material.albedoIndex != -1){
    vec4 temp = texture(textures[material.albedoIndex], in_texcoord);
    out_color *= toLinear(temp);
  }
  if (material.normalIndex != -1){
    vec3 temp = texture(textures[material.normalIndex], in_texcoord).xyz;
    temp = temp * 2.0 - 1.0;
    temp *= material.normal;
    temp = normalize(temp);

    vec3 bitangent = cross(in_normal, in_tangent);
    mat3 basis = mat3(in_tangent, bitangent, in_normal);
    out_normal = vec4(basis * temp, 1.0);
  } else {
    out_normal = vec4(in_normal, 1.0);
  }
}
