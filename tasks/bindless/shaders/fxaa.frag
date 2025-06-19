#version 450
#extension GL_GOOGLE_include_directive : require

#include "fxaa_defines.glsl"

vec3 toLinear(vec3 sRGB)
{
  bvec3 cutoff = lessThan(sRGB, vec3(0.04045));
  vec3 higher = pow((sRGB + vec3(0.055)) / vec3(1.055), vec3(2.4));
  vec3 lower = sRGB / vec3(12.92);

  return mix(higher, lower, cutoff);
}

vec3 fromLinear(vec3 linearRGB)
{
  bvec3 cutoff = lessThan(linearRGB, vec3(0.0031308));
  vec3 higher = vec3(1.055) * pow(linearRGB, vec3(1.0 / 2.4)) - vec3(0.055);
  vec3 lower = linearRGB * vec3(12.92);

  return mix(higher, lower, cutoff);
}

vec4 toLinear(vec4 sRGB)
{
  sRGB.rgb = toLinear(sRGB.rgb);
  return sRGB;
}

vec4 fromLinear(vec4 linearRGB)
{
  linearRGB.rgb = fromLinear(linearRGB.rgb);
  return linearRGB;
}

layout(push_constant, std430) uniform fxaa_pc
{
  vec2 rcpFrame;
  float subpix;
  float edgeThreshold;
  float edgeThresholdMin;
};

layout(binding = 0) uniform sampler2D inColor;
layout(location = 0) out vec4 outColor;

float getLuma(vec4 val)
{
#if (FXAA_GREEN_AS_LUMA == 0)
  return val.a;
#else
  return val.g;
#endif
}

void main()
{
  vec2 posM = gl_FragCoord.xy * rcpFrame;
  /*--------------------------------------------------------------------------*/
  // Read middle texel, read luma from neighborhood, define alias macros for luma

  vec4 rgblM = textureLod(inColor, posM, 0.0);
#if (FXAA_GREEN_AS_LUMA == 0)
#define lumaM rgblM.w
#else
#define lumaM rgblM.y
#endif

#if (FXAA_GREEN_AS_LUMA == 0)
  vec4 luma4A = textureGather(inColor, posM, 3);
  vec4 luma4B = textureGatherOffset(inColor, posM, ivec2(-1, -1), 3);
#else
  vec4 luma4A = textureGather(inColor, posM, 3);
  vec4 luma4B = textureGatherOffset(inColor, posM, ivec2(-1, -1), 3);
#endif
#define lumaE luma4A.z
#define lumaS luma4A.x
#define lumaSE luma4A.y
#define lumaNW luma4B.w
#define lumaN luma4B.z
#define lumaW luma4B.x
  /*--------------------------------------------------------------------------*/
  // Calculate local contrast, check if curr. pixel needs aa, otherwise exit
  float rangeMax = max(max(max(lumaS, lumaM), lumaE), max(lumaW, lumaN));
  float rangeMin = min(min(min(lumaS, lumaM), lumaE), min(lumaW, lumaN));
  float range = rangeMax - rangeMin;
  bool earlyExit = range < max(edgeThresholdMin, rangeMax * edgeThreshold);
  if (earlyExit)
  {
#if (FXAA_GREEN_AS_LUMA == 0)
    outColor = vec4(toLinear(rgblM.rgb), 1.0);
#else
    outColor = toLinear(rgblM);
#endif
    return;
  }
  /*--------------------------------------------------------------------------*/
  // get remaining neighboorhood luma
  float lumaNE = getLuma(textureOffset(inColor, posM, ivec2(1, -1)));
  float lumaSW = getLuma(textureOffset(inColor, posM, ivec2(-1, 1)));

  /*--------------------------------------------------------------------------*/
  // Calc subpix blend coefficient
  float neighborAvg = 2.0 * (lumaN + lumaE + lumaW + lumaS);
  neighborAvg += lumaNE + lumaNW + lumaSE + lumaSW;
  neighborAvg /= 12.0;
  float contrast = abs(neighborAvg - lumaM);
  float coef = smoothstep(0.0, range, contrast);
  coef *= coef;
  float pixelOffsetSubpix = coef * subpix;

  /*--------------------------------------------------------------------------*/
  // Determine whether the edge we're on is vertical or horizontal
  float edgeHorz = 2.0 * abs((-2.0 * lumaM) + lumaN + lumaS);
  edgeHorz += abs((-2.0 * lumaW) + lumaNW + lumaSW);
  edgeHorz += abs((-2.0 * lumaE) + lumaNE + lumaSE);

  float edgeVert = 2.0 * abs((-2.0 * lumaM) + lumaE + lumaW);
  edgeVert += abs((-2.0 * lumaN) + lumaNE + lumaNW);
  edgeVert += abs((-2.0 * lumaS) + lumaSE + lumaSW);

  bool isHorz = edgeHorz >= edgeVert;
  float lengthSign = isHorz ? rcpFrame.y : rcpFrame.x;

  /*--------------------------------------------------------------------------*/
  // Determine, if we're on the top or bottom pixel of the edge
  float lumaTop = isHorz ? lumaN : lumaW;
  float lumaBtm = isHorz ? lumaS : lumaE;

  float gradTop = lumaTop - lumaM;
  float gradBtm = lumaBtm - lumaM;
  bool isTop = gradTop >= gradBtm;
  if (isTop)
  {
    lengthSign = -lengthSign;
  }

  /*--------------------------------------------------------------------------*/
  // Offset position such that we're inbetween the edge pixels

  vec2 posEdge = posM;
  if (isHorz)
  {
    posEdge.y += 0.5 * lengthSign;
  }
  else
  {
    posEdge.x += 0.5 * lengthSign;
  }

  vec2 step = isHorz ? vec2(rcpFrame.x, 0) : vec2(0, rcpFrame.y);
  float startLumaAvg = isTop ? lumaTop + lumaM : lumaBtm + lumaM;
  startLumaAvg *= 0.5;
  float gradScaled = max(abs(gradTop), abs(gradBtm)) * 0.25;

  /*--------------------------------------------------------------------------*/
  // Make steps along the edge until local contrast is sufficiently different from the starting one

  vec2 posN = posEdge - step * FXAA_QUALITY_P0;
  vec2 posP = posEdge + step * FXAA_QUALITY_P0;
  float lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
  float lumaEndP = getLuma(textureLod(inColor, posP, 0.0));

  bool doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
  bool doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
  bool doneNP = (!doneN) || (!doneP);

  if (!doneN)
    posN -= step * FXAA_QUALITY_P1;
  if (!doneP)
    posP += step * FXAA_QUALITY_P1;
  if (doneNP)
  {
    if (!doneN)
      lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
    if (!doneP)
      lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
    doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
    doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
    doneNP = (!doneN) || (!doneP);
    if (!doneN)
      posN -= step * FXAA_QUALITY_P2;
    if (!doneP)
      posP += step * FXAA_QUALITY_P2;
#if (FXAA_QUALITY_PS > 3)
    if (doneNP)
    {
      if (!doneN)
        lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
      if (!doneP)
        lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
      doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
      doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
      doneNP = (!doneN) || (!doneP);
      if (!doneN)
        posN -= step * FXAA_QUALITY_P3;
      if (!doneP)
        posP += step * FXAA_QUALITY_P3;
#if (FXAA_QUALITY_PS > 4)
      if (doneNP)
      {
        if (!doneN)
          lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
        if (!doneP)
          lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
        doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
        doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
        doneNP = (!doneN) || (!doneP);
        if (!doneN)
          posN -= step * FXAA_QUALITY_P4;
        if (!doneP)
          posP += step * FXAA_QUALITY_P4;
#if (FXAA_QUALITY_PS > 5)
        if (doneNP)
        {
          if (!doneN)
            lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
          if (!doneP)
            lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
          doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
          doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
          doneNP = (!doneN) || (!doneP);
          if (!doneN)
            posN -= step * FXAA_QUALITY_P5;
          if (!doneP)
            posP += step * FXAA_QUALITY_P5;
#if (FXAA_QUALITY_PS > 6)
          if (doneNP)
          {
            if (!doneN)
              lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
            if (!doneP)
              lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
            doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
            doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
            doneNP = (!doneN) || (!doneP);
            if (!doneN)
              posN -= step * FXAA_QUALITY_P6;
            if (!doneP)
              posP += step * FXAA_QUALITY_P6;
#if (FXAA_QUALITY_PS > 7)
            if (doneNP)
            {
              if (!doneN)
                lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
              if (!doneP)
                lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
              doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
              doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
              doneNP = (!doneN) || (!doneP);
              if (!doneN)
                posN -= step * FXAA_QUALITY_P7;
              if (!doneP)
                posP += step * FXAA_QUALITY_P7;
#if (FXAA_QUALITY_PS > 8)
              if (doneNP)
              {
                if (!doneN)
                  lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
                if (!doneP)
                  lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
                doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
                doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
                doneNP = (!doneN) || (!doneP);
                if (!doneN)
                  posN -= step * FXAA_QUALITY_P8;
                if (!doneP)
                  posP += step * FXAA_QUALITY_P8;
#if (FXAA_QUALITY_PS > 9)
                if (doneNP)
                {
                  if (!doneN)
                    lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
                  if (!doneP)
                    lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
                  doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
                  doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
                  doneNP = (!doneN) || (!doneP);
                  if (!doneN)
                    posN -= step * FXAA_QUALITY_P9;
                  if (!doneP)
                    posP += step * FXAA_QUALITY_P9;
#if (FXAA_QUALITY_PS > 10)
                  if (doneNP)
                  {
                    if (!doneN)
                      lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
                    if (!doneP)
                      lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
                    doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
                    doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
                    doneNP = (!doneN) || (!doneP);
                    if (!doneN)
                      posN -= step * FXAA_QUALITY_P10;
                    if (!doneP)
                      posP += step * FXAA_QUALITY_P10;
#if (FXAA_QUALITY_PS > 11)
                    if (doneNP)
                    {
                      if (!doneN)
                        lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
                      if (!doneP)
                        lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
                      doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
                      doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
                      doneNP = (!doneN) || (!doneP);
                      if (!doneN)
                        posN -= step * FXAA_QUALITY_P11;
                      if (!doneP)
                        posP += step * FXAA_QUALITY_P11;
#if (FXAA_QUALITY_PS > 12)
                      if (doneNP)
                      {
                        if (!doneN)
                          lumaEndN = getLuma(textureLod(inColor, posN, 0.0));
                        if (!doneP)
                          lumaEndP = getLuma(textureLod(inColor, posP, 0.0));
                        doneN = abs(lumaEndN - startLumaAvg) >= gradScaled;
                        doneP = abs(lumaEndP - startLumaAvg) >= gradScaled;
                        doneNP = (!doneN) || (!doneP);
                        if (!doneN)
                          posN -= step * FXAA_QUALITY_P12;
                        if (!doneP)
                          posP += step * FXAA_QUALITY_P12;
                      }
#endif
                    }
#endif
                  }
#endif
                }
#endif
              }
#endif
            }
#endif
          }
#endif
        }
#endif
      }
#endif
    }
#endif
  }
  /*--------------------------------------------------------------------------*/
  // Determine blend direction and distance

  float dstN = isHorz ? posM.x - posN.x : posM.y - posN.y;
  float dstP = isHorz ? posP.x - posM.x : posP.y - posM.y;
  bool goodSpanN = (lumaEndN - startLumaAvg < 0.0) != (lumaM - startLumaAvg < 0.0);
  bool goodSpanP = (lumaEndN - startLumaAvg < 0.0) != (lumaM - startLumaAvg < 0.0);
  bool directionN = dstN < dstP;
  bool goodSpan = directionN ? goodSpanN : goodSpanP;
  float dst = min(dstN, dstP);
  float spanLength = (dstP + dstN);
  float pixelOffsetEdge = goodSpan ? 0.5 - (dst / spanLength) : 0.0;

  float pixelOffset = max(pixelOffsetSubpix, pixelOffsetEdge);
  if (isHorz)
  {
    posM.y += pixelOffset * lengthSign;
  }
  else
  {
    posM.x += pixelOffset * lengthSign;
  }


  vec4 colorAdj = textureLod(inColor, posM, 0.0);
#if (FXAA_GREEN_AS_LUMA == 0)
  outColor = vec4(toLinear(colorAdj.rgb), 1.0);
#else
  outColor = toLinear(colorAdj);
#endif
  return;
}
