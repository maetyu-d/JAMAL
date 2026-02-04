#include <metal_stdlib>
using namespace metal;

struct VOut {
    float4 position [[position]];
    float2 uv;
};

struct VisualUniforms {
    float2 resolution;
    float time;
    float level;
    float day;
    float portal;
    float beat;
};

vertex VOut visuals_vertex(uint vid [[vertex_id]]) {
    float2 verts[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VOut out;
    out.position = float4(verts[vid], 0.0, 1.0);
    out.uv = (out.position.xy * 0.5 + 0.5);
    return out;
}

static float hash21(float2 p) {
    p = fract(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

static float noise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

static float fbm(float2 p) {
    float v = 0.0;
    float a = 0.55;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p *= 2.05;
        a *= 0.5;
    }
    return v;
}

static float2 kalei(float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    float r = length(p);
    float a = atan2(p.y, p.x);
    float slices = 10.0;
    const float PI = 3.14159265;
    a = fmod(a, 2.0 * PI / slices);
    a = abs(a - (PI / slices));
    return float2(cos(a), sin(a)) * r;
}

fragment float4 visuals_fragment(VOut in [[stage_in]],
                                 constant VisualUniforms &u [[buffer(0)]]) {
    float2 uv = in.uv;
    float2 p = uv * 2.0 - 1.0;
    p.x *= u.resolution.x / max(u.resolution.y, 1.0);

    float audio = clamp(u.level * 2.2 + u.beat * 0.8, 0.0, 2.0);
    float t = u.time * 0.02 + audio * 0.15;
    float zoom = 1.0 + 0.35 * u.portal + 0.15 * sin(t * 2.0 + audio);

    float2 k = kalei(uv);
    float2 z = (p + k * 0.35) * zoom;

    float accum = 0.0;
    float glow = 0.0;
    float scale = 1.0;
    for (int i = 0; i < 6; i++) {
        float2 q = z * scale + float2(cos(t * 0.7 + i), sin(t * 0.9 - i));
        float n = fbm(q * 1.6 + float2(i * 1.3, -i * 0.9));
        float ring = exp(-abs(n - 0.5) * (8.0 + audio * 6.0));
        accum += ring / scale;
        glow += n * 0.35;
        scale *= 1.6;
    }

    float m = length(p);
    float portal = exp(-m * (3.5 + u.portal * 3.0)) * (0.6 + u.beat * 0.8);
    float scan = sin((p.x + p.y + t * 2.0) * 8.0) * 0.06 * (0.3 + audio);

    float3 base = float3(0.08, 0.06, 0.14) + glow * float3(0.18, 0.12, 0.25);
    float3 colorA = float3(0.1, 0.6, 0.9);
    float3 colorB = float3(0.9, 0.2, 0.8);
    float3 colorC = float3(0.9, 0.7, 0.2);

    float mix1 = clamp(accum * 0.45 + audio * 0.2, 0.0, 1.0);
    float mix2 = clamp(glow * 0.4 + u.portal * 0.6, 0.0, 1.0);
    float3 col = mix(base, colorA, mix1);
    col = mix(col, colorB, mix2);
    col = mix(col, colorC, clamp(portal * 0.8 + u.beat * 0.3, 0.0, 1.0));

    col += scan + portal * 0.6;
    col = pow(max(col, 0.0), float3(0.85));
    return float4(col, 1.0);
}
