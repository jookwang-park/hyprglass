// Auto-generated shader header - Do not edit!
#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader — Thick-glass refraction model
 *
 * The window is modeled as a thick convex glass slab:
 *   - Center: flat surface → clean frosted blur, no distortion
 *   - Edges: curved surface → refraction pulls in content from beyond
 *     the window boundary, creating natural color bleeding
 *
 * Rendering layers:
 * 1. Edge refraction via smooth outward direction + exponential proximity
 * 2. Chromatic aberration (per-channel refraction scale)
 * 3. Edge raw-texture blend for vivid color pickup
 * 4. Subtle center dome lens magnification
 * 5. Frosted tint (brightness boost + desaturation)
 * 6. Configurable color tint overlay
 * 7. Fresnel edge glow
 * 8. Specular highlight (top)
 * 9. Inner shadow (bottom rim)
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform float radius;
uniform vec2 uvPadding;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float vibrancy;
uniform float vibrancyDarkness;
uniform float adaptiveDim;
uniform float adaptiveBoost;
uniform float roundingPower;
uniform float timeSeconds;
uniform int rainEnabled;
uniform float rainIntensity;
uniform float rainSpeed;
uniform float rainScale;
uniform float rainDistortion;

uniform sampler2D maskTex;
uniform int useMask;
uniform vec2 maskUVOffset;
uniform vec2 maskUVScale;
uniform float maskAlphaThreshold;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleBlurred(vec2 wuv) {
    vec2 tuv = toTexUV(wuv);
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF
// ============================================================================

float lpNorm(vec2 v, float p) {
    return pow(pow(abs(v.x), p) + pow(abs(v.y), p), 1.0 / p);
}

float getRoundedBoxSDF(vec2 uv, float r) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + lpNorm(max(q, 0.0), roundingPower) - clampedR;
}

float getCornerSDF(vec2 uv) {
    return getRoundedBoxSDF(uv, radius);
}

// ============================================================================
// REFRACTION DIRECTION
// Pixel-space direction toward window center — perfectly smooth everywhere,
// no SDF gradient needed. On straight edges the perpendicular pixel distance
// dominates, giving approximately edge-normal direction. At corners it
// naturally follows the diagonal.
// ============================================================================

vec2 refractionDir(vec2 uv) {
    vec2 toCenterPx = (vec2(0.5) - uv) * fullSize;
    float len = length(toCenterPx);
    return len > 0.1 ? toCenterPx / len : vec2(0.0);
}

// ============================================================================
// OPTIONAL RAIN OVERLAY — textureless procedural droplets
// ============================================================================

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float rainDropCell(vec2 gv, vec2 id, float density, float speed) {
    float rnd = hash21(id);
    float activeMask = step(1.0 - density, rnd);

    float sizeRnd = hash21(id + vec2(11.3, 19.7));
    float dropSize = mix(0.65, 1.75, sizeRnd);
    float width = mix(0.026, 0.060, sizeRnd);
    float height = width * mix(1.35, 2.05, hash21(id + vec2(4.7, 2.9)));

    float x = 0.14 + 0.72 * hash21(id + vec2(3.1, 7.7));
    float y = fract(timeSeconds * speed * (0.16 + rnd * 0.55) + rnd);
    float fadeOut = 1.0 - smoothstep(0.58, 1.0, y);
    float lifeFade = fadeOut;
    float sizeFade = mix(0.18, 1.0, fadeOut);
    vec2 d = vec2(gv.x - x, gv.y - y);

    float taper = smoothstep(-height, height, d.y);
    float localWidth = width * dropSize * sizeFade * mix(0.58, 1.18, taper);
    float localHeight = height * dropSize * sizeFade;
    float bodyMetric = length(vec2(d.x / localWidth, d.y / localHeight));
    float body = 1.0 - smoothstep(0.78, 1.02, bodyMetric);

    vec2 beadD = vec2(d.x / (localWidth * 0.85), (d.y - localHeight * 0.28) / (localHeight * 0.55));
    float bead = 1.0 - smoothstep(0.45, 0.98, length(beadD));

    float trailLength = mix(0.10, 0.34, sizeRnd) * sizeFade;
    float tailY = -d.y - localHeight * 0.18;
    float trailWidth = localWidth * mix(0.08, 0.16, sizeRnd) * (1.0 - smoothstep(0.0, trailLength, tailY));
    float trail = (1.0 - smoothstep(trailWidth, trailWidth * 2.4, abs(d.x)))
                * smoothstep(0.0, localHeight * 0.28, tailY)
                * (1.0 - smoothstep(trailLength * 0.45, trailLength, tailY));

    float drop = max(body, bead * 0.85);
    return activeMask * lifeFade * max(drop, trail * mix(0.06, 0.14, sizeRnd));
}

float rainLayer(vec2 uv, float density, float speed, float scale) {
    vec2 p = uv * vec2(16.0, 10.0) * scale;
    vec2 id = floor(p);
    vec2 gv = fract(p);

    // Sample neighboring vertical cells too, so droplets/trails crossing a
    // cell boundary fade naturally instead of being clipped by fract().
    float center = rainDropCell(gv, id, density, speed);
    float above = rainDropCell(gv + vec2(0.0, 1.0), id - vec2(0.0, 1.0), density, speed);
    float below = rainDropCell(gv - vec2(0.0, 1.0), id + vec2(0.0, 1.0), density, speed);
    return max(center, max(above, below));
}

float rainField(vec2 uv) {
    float density = clamp(rainIntensity, 0.0, 1.0);
    float scale = max(rainScale, 0.1);
    float speed = max(rainSpeed, 0.0);
    float a = rainLayer(uv, density * 0.78, speed, scale);
    float b = rainLayer(uv * 1.37 + vec2(0.17, 0.41), density * 0.42, speed * 1.35, scale * 1.28);
    return clamp(max(a, b * 0.78), 0.0, 1.0);
}

float rainStreakField(vec2 uv) {
    float density = clamp(rainIntensity, 0.0, 1.0);
    float scale = max(rainScale, 0.1);
    float speed = max(rainSpeed, 0.0);

    vec2 slanted = vec2(uv.x + uv.y * 0.18, uv.y);
    vec2 p = slanted * vec2(42.0, 18.0) * scale;
    vec2 id = floor(p);
    vec2 gv = fract(p);

    float rnd = hash21(id);
    float activeMask = step(1.0 - density * 0.55, rnd);
    float y = fract(gv.y + timeSeconds * speed * (0.9 + rnd * 0.8));
    float x = 0.5 + (hash21(id + vec2(9.1, 2.4)) - 0.5) * 0.32;

    float line = (1.0 - smoothstep(0.010, 0.030, abs(gv.x - x)))
               * smoothstep(0.02, 0.16, y)
               * (1.0 - smoothstep(0.20, 0.72, y));

    return activeMask * line * 0.55;
}

float rainWaterField(vec2 uv) {
    return rainField(uv);
}

// ============================================================================
// MAIN — Thick-glass refraction model
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Layers only: sample the temp FBO to get the rendered surface pixel.
    // Discard fully transparent fragments so glass only covers visible content.
    // For windows, hasMask is false and this block is skipped entirely.
    vec4 surfacePixel = vec4(0.0);
    bool hasMask = (useMask == 1);
    if (hasMask) {
        vec2 maskUV = uv * maskUVScale + maskUVOffset;
        surfacePixel = texture(maskTex, clamp(maskUV, 0.001, 0.999));
        if (surfacePixel.a < maskAlphaThreshold) discard;
    }

    float cornerSdf = getCornerSDF(uv);

    if (cornerSdf > 0.0) {
        discard;
    }

    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (cornerAlpha < 0.001) discard;

    float minDim = min(fullSize.x, fullSize.y);
    float bezelWidthPx = edgeThickness * minDim;

    // ========================================
    // EDGE PROXIMITY + DIRECTION
    // edgeProximity: 1.0 at boundary, exponential decay inward
    // inwardDir: pixel-space direction toward center (smooth everywhere)
    // ========================================
    float edgeProximity = exp(cornerSdf / bezelWidthPx);
    vec2 inwardDir = refractionDir(uv);

    // ========================================
    // EDGE REFRACTION
    // Offset sampling UV inward (toward center) at edges — like looking
    // through the curved thick edge of a glass slab. This compresses
    // and distorts what's already behind the window, without reaching
    // beyond the window boundary.
    // ========================================
    float refractionPx = refractionStrength * 50.0;
    float refractionMag = edgeProximity * refractionPx;
    vec2 baseOffset = inwardDir * refractionMag / fullSize;

    // ========================================
    // CHROMATIC ABERRATION — per-channel refraction scale
    // Blue refracts more than red → natural spectral fringing at edges.
    // ========================================
    float chromaSpread = chromaticAberration * 0.35;
    vec2 offsetR = baseOffset * (1.0 - chromaSpread);
    vec2 offsetG = baseOffset;
    vec2 offsetB = baseOffset * (1.0 + chromaSpread);

    // ========================================
    // CENTER DOME LENS (subtle magnification in the flat interior)
    // Fades near edges so it doesn't interfere with edge refraction.
    // ========================================
    vec2 domeUV = vec2(0.0);
    if (lensDistortion > 0.001) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 dGrad = vec2(
            -4.0 * c.x * (1.0 - c.y * c.y),
            -4.0 * c.y * (1.0 - c.x * c.x)
        );
        float lensMaxPx = lensDistortion * minDim * 0.006;
        float lensFade = 1.0 - edgeProximity;
        domeUV = dGrad * lensMaxPx * lensFade / fullSize;
    }

    // ========================================
    // BACKGROUND SAMPLING (frosted blur only)
    // Nearby color influence comes naturally from the Gaussian blur
    // kernel crossing the window boundary — no explicit raw sampling.
    // ========================================
    float rain = 0.0;
    float rainStreak = 0.0;
    float rainEdge = 0.0;
    float rainSpec = 0.0;
    vec2 rainUVOffset = vec2(0.0);
    if (rainEnabled == 1 && rainIntensity > 0.001) {
        rain = rainWaterField(uv);
        rainStreak = rainStreakField(uv);
        float px = 1.0 / max(minDim, 1.0);
        vec2 n = vec2(
            rainWaterField(uv + vec2(px, 0.0)) - rainWaterField(uv - vec2(px, 0.0)),
            rainWaterField(uv + vec2(0.0, px)) - rainWaterField(uv - vec2(0.0, px))
        );
        float nLen = length(n);
        vec2 nDir = n / max(nLen, 0.0001);
        rainEdge = smoothstep(0.015, 0.12, nLen);
        rainSpec = pow(max(dot(nDir, normalize(vec2(-0.45, -0.9))), 0.0), 3.0) * rainEdge;
        rainUVOffset = -n * max(rainDistortion, 0.0);
    }

    vec3 color;
    vec2 uvR = uv + offsetR + domeUV + rainUVOffset;
    vec2 uvG = uv + offsetG + domeUV + rainUVOffset;
    vec2 uvB = uv + offsetB + domeUV + rainUVOffset;

    if (chromaticAberration > 0.001 && edgeProximity > 0.01) {
        color.r = sampleBlurred(uvR).r;
        color.g = sampleBlurred(uvG).g;
        color.b = sampleBlurred(uvB).b;
    } else {
        color = sampleBlurred(uvG).rgb;
    }

    // ========================================
    // FROSTED TINT (per-theme tone mapping)
    // ========================================
    float blurredLum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Frosted desaturation
    color = mix(vec3(blurredLum), color, saturation);

    // Tight smoothstep range maps the blur-compressed luminance (~0.3-0.7)
    // to the full [0,1] adaptive range, creating visible per-region differentiation
    float lumCurve = smoothstep(0.25, 0.55, blurredLum);

    // Dim: multiplicative — effective at darkening bright areas
    color *= brightness * (1.0 - adaptiveDim * lumCurve);

    // Boost: additive lift — multiplicative can't brighten near-black content
    color += vec3(adaptiveBoost * (1.0 - lumCurve) * 0.5);

    // Contrast (pivot around midpoint)
    color = mix(vec3(0.5), color, contrast);

    // Vibrancy (selective saturation boost scaled by existing saturation)
    float currentLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    float darkFactor = 1.0 - vibrancyDarkness * (1.0 - blurredLum);
    color = mix(vec3(currentLum), color, 1.0 + vibrancy * sat * darkFactor);

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // RAIN HIGHLIGHT / REFRACTION CUES
    // ========================================
    if (rainEnabled == 1 && (rain > 0.001 || rainStreak > 0.001)) {
        color *= 1.0 - rain * rainIntensity * 0.06;
        color += vec3(0.62, 0.76, 1.0) * rainStreak * (0.035 + rainIntensity * 0.08);
        color += vec3(0.75, 0.86, 1.0) * rainEdge * (0.06 + rainIntensity * 0.16);
        color += vec3(1.0, 0.98, 0.92) * rainSpec * (0.16 + rainIntensity * 0.32);
    }

    // ========================================
    // FRESNEL RIM GLOW (edge zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.15;
        color += vec3(1.0) * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (edge zone)
    // ========================================
    if (specularStrength > 0.001) {
        float topBias = pow(max(1.0 - uv.y, 0.0), 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // INNER SHADOW (bottom rim)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

    float glassA = glassOpacity * cornerAlpha;

    if (hasMask) {
        // Layers only: composite the rendered surface over the glass effect
        // in a single pass. surfacePixel is premultiplied alpha from Hyprland's
        // surface rendering, so we unpremultiply before the 'over' blend.
        float surfA = surfacePixel.a;
        vec3 surfRGB = surfA > 0.001 ? surfacePixel.rgb / surfA : vec3(0.0);

        float compA = surfA + glassA * (1.0 - surfA);
        vec3 compRGB = compA > 0.001
            ? (surfRGB * surfA + color * glassA * (1.0 - surfA)) / compA
            : vec3(0.0);

        // Hyprland's compositor expects premultiplied alpha (blend GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
        fragColor = vec4(compRGB * compA, compA);
    } else {
        // Windows: output the glass effect alone, surface is rendered separately by Hyprland.
        // Premultiplied: without this, a fading window's glass keeps full RGB contribution
        // because the GL_ONE source factor adds raw color regardless of alpha.
        fragColor = vec4(color * glassA, glassA);
    }
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    int samples = min(int(ceil(blurRadius)), 8);

    // Center tap
    float w0 = 1.0;
    vec4 result = texture(tex, v_texcoord) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += texture(tex, v_texcoord + direction * offset) * wSum;
        result += texture(tex, v_texcoord - direction * offset) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
