/* Vita Cg dialect for the shared NV2A generators. Arithmetic and combiner
 * decoding remain identical to the desktop renderer. */
#ifndef HALO_NV2A_CG_H
#define HALO_NV2A_CG_H
#define NV2A_CG_TYPES \
    "#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n" \
    "#define mat4 float4x4\n#define fract frac\n#define mod fmod\n" \
    "#define mix lerp\n#define inversesqrt rsqrt\n" \
    "#define lessThan(a,b) ((a)<(b))\n#define greaterThanEqual(a,b) ((a)>=(b))\n" \
    "#define texture2D tex2D\n#define textureCube texCUBE\n"
#define NV2A_CG_OUTPUTS \
    "varying out float4 xD0 : COLOR0;\nvarying out float4 xD1 : COLOR1;\n" \
    "varying out float4 xT0 : TEXCOORD0;\nvarying out float4 xT1 : TEXCOORD1;\n" \
    "varying out float4 xT2 : TEXCOORD2;\nvarying out float4 xT3 : TEXCOORD3;\n" \
    "varying out float xFog : TEXCOORD4;\n" \
    "varying out float4 gl_Position : POSITION;\n"
#endif
