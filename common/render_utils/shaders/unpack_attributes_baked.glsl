#ifndef UNPACK_ATTRIBUTES_GLSL_INCLUDED
#define UNPACK_ATTRIBUTES_GLSL_INCLUDED

// NOTE: .glsl extension is used for helper files with shader code

vec3 decode_normal(uint a_data)
{
  const uint a_enc_x = (a_data & 0x000000FFu) >> 0;
  const uint a_enc_y = (a_data & 0x0000FF00u) >> 8;
  const uint a_enc_z = (a_data & 0x00FF0000u) >> 16;

  ivec3 enc_i = ivec3(a_enc_x, a_enc_y, a_enc_z);
  enc_i += 128;
  enc_i %= 256;
  enc_i -= 128;
  vec3 enc = vec3(enc_i);

  return max(enc / 127.0, -1.0);
}

#endif // UNPACK_ATTRIBUTES_GLSL_INCLUDED
