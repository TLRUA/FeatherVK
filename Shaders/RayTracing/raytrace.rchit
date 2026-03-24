#version 460

#include "RayTracingGlobal.glsl"
#include "../PBR.glsl"
#include "../Utils/random.glsl"

layout (location = 0) rayPayloadInEXT hitPayLoad payLoad;
layout (location = 1) rayPayloadEXT ShadowPayload shadowPayload;

hitAttributeEXT vec3 attribs;

layout (set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout (set = 1, binding = 1, std430) readonly buffer EntityDescBuffer {EntityDesc entityDescs[];} entityDescBuffer;
layout (set = 1, binding = 2) uniform sampler2D textureSamplers[];
layout (set = 1, binding = 3) uniform samplerCube skyboxSampler;

const float PrimaryRayBias = 0.005;
const float ShadowRayBias = 0.01;
const float MinimumBounceThroughput = 0.02;
const int ShadowSampleCount = 5;
const int EnvironmentDiffuseSampleCount = 9;
const float DirectionalLightAngularRadius = 0.0025;
const float PointLightRadiusScale = 0.01;
const float MinimumPointLightRadius = 0.02;
const vec2 ShadowKernel[4] = vec2[](
    vec2(-0.375, -0.125),
    vec2(0.125, -0.375),
    vec2(-0.125, 0.375),
    vec2(0.375, 0.125)
);

PBR reloadPBR(PBR rawPBR, ivec2 textureEntry, vec2 uv, vec3 normal, mat3 TBN) {
    PBR pbr;
    int textureIndex = textureEntry.x;

    if (rawPBR.albedo == vec3(-1, -1, -1)) {
        pbr.albedo = texture(textureSamplers[textureIndex], uv).xyz;
        textureIndex++;
    } else {
        pbr.albedo = rawPBR.albedo;
    }

    if (rawPBR.normal == vec3(-1, -1, -1)) {
        vec3 texNormal = texture(textureSamplers[textureIndex], uv).xyz;
        pbr.normal = texNormal * 2.0 - 1.0;
        pbr.normal = TBN * pbr.normal;
        textureIndex++;
    } else {
        pbr.normal = normal;
    }

    if (rawPBR.metallic == -1) {
        pbr.metallic = texture(textureSamplers[textureIndex], uv).x;
        textureIndex++;
    } else {
        pbr.metallic = rawPBR.metallic;
    }

    if (rawPBR.roughness == -1) {
        pbr.roughness = texture(textureSamplers[textureIndex], uv).x;
        textureIndex++;
    } else {
        pbr.roughness = rawPBR.roughness;
    }

    if (rawPBR.opacity == -1) {
        pbr.opacity = texture(textureSamplers[textureIndex], uv).x;
        textureIndex++;
    } else {
        pbr.opacity = rawPBR.opacity;
    }

    if (rawPBR.AO == -1) {
        pbr.AO = texture(textureSamplers[textureIndex], uv).x;
    } else {
        pbr.AO = 1;
    }

    if (rawPBR.emissive == vec3(-1, -1, -1)) {
        pbr.emissive = texture(textureSamplers[textureIndex], uv).xyz;
    } else {
        pbr.emissive = rawPBR.emissive;
    }

    return pbr;
}

float maxComponent(vec3 value) {
    return max(value.x, max(value.y, value.z));
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sampleEnvironmentRadiance(vec3 direction) {
    vec3 cubeMapUV = normalize(direction);
    cubeMapUV.y = -cubeMapUV.y;
    return texture(skyboxSampler, cubeMapUV).rgb;
}

vec3 offsetRayOrigin(vec3 origin, vec3 normal, vec3 direction, float bias) {
    float signValue = dot(direction, normal) >= 0.0 ? 1.0 : -1.0;
    return origin + normal * (bias * signValue);
}

float computeAdaptiveShadowBias(vec3 geometricNormal, vec3 lightDirection) {
    float normalToLight = clamp(dot(geometricNormal, lightDirection), 0.0, 1.0);
    return mix(ShadowRayBias * 4.0, ShadowRayBias, normalToLight);
}

float traceShadowVisibility(vec3 worldPos, vec3 geometricNormal, vec3 lightDirection, float maxDistance) {
    float shadowBias = computeAdaptiveShadowBias(geometricNormal, lightDirection);
    float tMin = shadowBias;
    float tMax = max(maxDistance - shadowBias, tMin + 0.001);
    vec3 origin = offsetRayOrigin(worldPos, geometricNormal, lightDirection, shadowBias);
    uint flags = gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsCullBackFacingTrianglesEXT | gl_RayFlagsTerminateOnFirstHitEXT;
    shadowPayload.opaqueShadowed = true;
    shadowPayload.opacity = 0.0;
    traceRayEXT(topLevelAS, flags, DEFAULT_RENDER_LAYER_MASK, 0, 0, 1, origin, tMin, lightDirection, tMax, 1);
    if (shadowPayload.opaqueShadowed) {
        return 0.0;
    }
    return clamp(1.0 - shadowPayload.opacity, 0.0, 1.0);
}

vec3 sampleShadowDirection(vec3 worldPos, Light light, vec3 baseDirection, float baseDistance, vec2 kernelOffset, out float sampleDistance) {
    mat3 basis = orthonormalBasis(baseDirection);
    vec3 offsetDirection = basis[0] * kernelOffset.x + basis[1] * kernelOffset.y;
    if (light.lightCategory == 0) {
        float lightRadius = max(MinimumPointLightRadius, baseDistance * PointLightRadiusScale);
        vec3 sampledLightPosition = light.position.xyz + offsetDirection * lightRadius;
        vec3 sampledToLight = sampledLightPosition - worldPos;
        sampleDistance = length(sampledToLight);
        return sampledToLight / max(sampleDistance, 0.0001);
    }

    sampleDistance = baseDistance;
    return normalize(baseDirection + offsetDirection * DirectionalLightAngularRadius);
}

const int FirstBounceReflectionSamples = 1;
const float ReflectionRayFadeStart = 0.28;
const float ReflectionRayFadeEnd = 0.42;
const float MirrorRoughnessThreshold = 0.002;
const float MaxReflectionContributionLuminance = 16.0;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-10 ? value * inversesqrt(lengthSquared) : fallback;
}

mat3 buildObjectTBN(vec3 deltaPos1, vec3 deltaPos2, vec2 deltaUV1, vec2 deltaUV2, vec3 objectNormal) {
    vec3 safeNormal = safeNormalize(objectNormal, vec3(0.0, 0.0, 1.0));
    float determinant = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
    if (abs(determinant) < 1e-8) {
        return orthonormalBasis(safeNormal);
    }

    mat3 fallbackBasis = orthonormalBasis(safeNormal);
    vec3 tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) / determinant;
    tangent = safeNormalize(tangent - safeNormal * dot(tangent, safeNormal), fallbackBasis[0]);
    float handedness = determinant < 0.0 ? -1.0 : 1.0;
    vec3 bitangent = safeNormalize(cross(safeNormal, tangent) * handedness, fallbackBasis[1]);
    return mat3(tangent, bitangent, safeNormal);
}

int reflectionSampleCount(int bounceCount) {
    return bounceCount == 0 ? FirstBounceReflectionSamples : 1;
}

vec3 buildReflectionDirection(vec3 worldNormal, vec3 pixelToView, float roughness, float sampleSeed) {
    vec3 perfectReflection = safeNormalize(reflect(-pixelToView, worldNormal), worldNormal);
    if (roughness <= MirrorRoughnessThreshold) {
        return perfectReflection;
    }

    return safeNormalize(sampleGGXReflection(worldNormal, pixelToView, roughness, sampleSeed), perfectReflection);
}

float stableReflectionSeed(vec3 worldPos, int sampleIndex, int bounceCount) {
    vec2 launch = vec2(gl_LaunchIDEXT.xy);
    return dot(launch, vec2(0.75487766, 0.56984029))
         + dot(worldPos, vec3(0.1031, 0.11369, 0.13787))
         + float(gl_PrimitiveID) * 0.0973
         + float(gl_InstanceCustomIndexEXT) * 0.1937
         + float(bounceCount) * 0.3719
         + float(sampleIndex) * 0.61803399;
}

vec3 clampReflectionContribution(vec3 contribution) {
    contribution = max(contribution, vec3(0.0));
    float contributionLuminance = luminance(contribution);
    if (contributionLuminance > MaxReflectionContributionLuminance) {
        contribution *= MaxReflectionContributionLuminance / contributionLuminance;
    }
    return contribution;
}

vec3 sampleStableGlossyEnvironment(vec3 reflectionDirection, float roughness) {
    vec3 direction = safeNormalize(reflectionDirection, vec3(0.0, 1.0, 0.0));
    mat3 basis = orthonormalBasis(direction);
    float coneRadius = clamp(roughness * roughness * 1.35, 0.0, 0.95);

    const vec2 sampleDisk[8] = vec2[](
        vec2(0.0, 0.0),
        vec2(0.7071, 0.0),
        vec2(-0.7071, 0.0),
        vec2(0.0, 0.7071),
        vec2(0.0, -0.7071),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5),
        vec2(0.5, -0.5)
    );

    vec3 radiance = vec3(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 disk = sampleDisk[i] * coneRadius;
        float z = sqrt(max(1.0 - dot(disk, disk), 0.0));
        vec3 sampleDirection = safeNormalize(basis * vec3(disk, z), direction);
        float weight = i == 0 ? 2.0 : 1.0;
        radiance += sampleEnvironmentRadiance(sampleDirection) * weight;
        totalWeight += weight;
    }

    return radiance / max(totalWeight, 0.0001);
}

vec3 estimateDiffuseIrradiance(vec3 worldNormal) {
    vec3 normal = safeNormalize(worldNormal, vec3(0.0, 1.0, 0.0));
    mat3 basis = orthonormalBasis(normal);
    vec3 irradiance = sampleEnvironmentRadiance(normal) * 0.25;
    float totalWeight = 0.25;

    const vec2 sampleDisk[8] = vec2[](
        vec2(0.7071, 0.0),
        vec2(-0.7071, 0.0),
        vec2(0.0, 0.7071),
        vec2(0.0, -0.7071),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5),
        vec2(0.5, -0.5),
        vec2(-0.5, -0.5)
    );

    for (int i = 0; i < EnvironmentDiffuseSampleCount - 1; ++i) {
        vec2 disk = sampleDisk[i];
        float z = sqrt(max(1.0 - dot(disk, disk), 0.0));
        vec3 sampleDirection = normalize(basis * vec3(disk, z));
        float weight = max(dot(normal, sampleDirection), 0.0);
        irradiance += sampleEnvironmentRadiance(sampleDirection) * weight;
        totalWeight += weight;
    }

    return irradiance / max(totalWeight, 0.0001);
}

vec3 evaluateEnvironmentDiffuse(vec3 worldNormal, vec3 pixelToView, PBR pbr) {
    vec3 f0 = baseReflectivity(pbr.albedo, pbr.metallic);
    vec3 fresnel = fresnelSchlickFunction(max(dot(worldNormal, pixelToView), 0.0), f0);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - pbr.metallic);
    return diffuseWeight * pbr.albedo * estimateDiffuseIrradiance(worldNormal) * pbr.AO;
}

void main()
{
    payLoad.recursionDepth++;
    if (payLoad.recursionDepth >= MAX_RECURSION_DEPTH) {
        payLoad.recursionDepth--;
        return;
    }

    EntityDesc entityDesc = entityDescBuffer.entityDescs[gl_InstanceCustomIndexEXT];
    VerticesBuffer verticesBuffer = VerticesBuffer(entityDesc.verticesAddress);
    IndicesBuffer indicesBuffer = IndicesBuffer(entityDesc.indicesAddress);

    uint i0 = indicesBuffer.indices[gl_PrimitiveID * 3];
    uint i1 = indicesBuffer.indices[gl_PrimitiveID * 3 + 1];
    uint i2 = indicesBuffer.indices[gl_PrimitiveID * 3 + 2];
    Vertex v0 = verticesBuffer.vertices[i0];
    Vertex v1 = verticesBuffer.vertices[i1];
    Vertex v2 = verticesBuffer.vertices[i2];

    vec3 barycentric = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 pos = barycentric.x * v0.position + barycentric.y * v1.position + barycentric.z * v2.position;
    vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));
    vec3 normal = normalize(barycentric.x * v0.normal + barycentric.y * v1.normal + barycentric.z * v2.normal);
    vec2 uv = barycentric.x * v0.uv + barycentric.y * v1.uv + barycentric.z * v2.uv;

    vec3 deltaPos1 = v1.position - v0.position;
    vec3 deltaPos2 = v2.position - v0.position;
    vec2 deltaUV1 = v1.uv - v0.uv;
    vec2 deltaUV2 = v2.uv - v0.uv;
    mat3 TBN = buildObjectTBN(deltaPos1, deltaPos2, deltaUV1, deltaUV2, normal);

    PBR pbr = reloadPBR(entityDesc.pbr, entityDesc.textureEntry, uv, normal, TBN);
    pbr.roughness = clamp(pbr.roughness, 0.02, 1.0);
    pbr.opacity = clamp(pbr.opacity, 0.0, 1.0);
    mat3 worldNormalMatrix = transpose(mat3(gl_WorldToObjectEXT));
    vec3 objectShadingNormal = safeNormalize(pbr.normal, normal);
    vec3 objectGeometricNormal = safeNormalize(cross(deltaPos1, deltaPos2), normal);
    vec3 worldNormal = safeNormalize(worldNormalMatrix * objectShadingNormal, vec3(0.0, 1.0, 0.0));
    vec3 worldGeometricNormal = safeNormalize(worldNormalMatrix * objectGeometricNormal, worldNormal);
    worldGeometricNormal = faceforward(worldGeometricNormal, gl_WorldRayDirectionEXT, worldGeometricNormal);
    if (dot(worldNormal, worldGeometricNormal) < 0.0) {
        worldNormal = -worldNormal;
    }
    vec3 pixelToView = normalize(-gl_WorldRayDirectionEXT);
    const bool receivesShadow = (entityDesc.renderOptions & ENTITY_RENDER_OPTION_RECEIVE_SHADOW) != 0;
    const bool isPrimarySurface = payLoad.recursionDepth == 1 && !payLoad.isBouncing;
    const float primaryContributionWeight = payLoad.isBouncing ? pbr.opacity : (1.0 - payLoad.opacity) * pbr.opacity;

    if (payLoad.recursionDepth == 1) {
        payLoad.closestHitWorldPos = vec4(worldPos, 1.0);
        payLoad.primaryMaterialGuide = vec4(pbr.roughness, pbr.metallic, 1.0, 0.0);
    }

    vec3 lo = vec3(0.0);
    vec3 primaryDirectLighting = vec3(0.0);
    float shadowVisibilityNumerator = 0.0;
    float shadowVisibilityDenominator = 0.0;

    for (int i = 0; i < ubo.lightNum; i++) {
        Light light = ubo.lights[i];
        vec3 pixelToLight;
        float attenuation = 1.0;
        float shadowRayDistance = 10000.0;
        switch (light.lightCategory) {
            case -1:
                continue;
            case 0:
                float distanceToLight = length(light.position.xyz - worldPos);
                shadowRayDistance = distanceToLight;
                attenuation = min(1.0, 1.0f / (distanceToLight * distanceToLight));
                pixelToLight = normalize(light.position.xyz - worldPos);
                break;
            case 1:
                pixelToLight = normalize(-light.direction.xyz);
                break;
            case 2:
                pixelToLight = normalize(light.position.xyz);
                break;
        }

        const vec3 brdf = cookTorrenceBRDF(worldNormal, pixelToView, pixelToLight, pbr.albedo, pbr.roughness, pbr.metallic);
        const vec3 radiance = light.color.xyz * light.color.w;
        const float geometry = clamp(dot(pixelToLight, worldNormal), 0.0, 1.0);
        const vec3 unshadowedLighting = radiance * brdf * geometry * attenuation;

        float shadowMask = 1.0;
        if (receivesShadow && (geometry > 0.001 || pbr.opacity < 0.99)) {
            float visibility = traceShadowVisibility(worldPos, worldGeometricNormal, pixelToLight, shadowRayDistance);
            for (int sampleIndex = 0; sampleIndex < ShadowSampleCount - 1; ++sampleIndex) {
                float sampleDistance = shadowRayDistance;
                vec3 sampleDirection = sampleShadowDirection(worldPos, light, pixelToLight, shadowRayDistance, ShadowKernel[sampleIndex], sampleDistance);
                visibility += traceShadowVisibility(worldPos, worldGeometricNormal, sampleDirection, sampleDistance);
            }
            shadowMask = visibility / float(ShadowSampleCount);
        }

        lo += unshadowedLighting * shadowMask;
        if (isPrimarySurface) {
            vec3 directBase = unshadowedLighting * primaryContributionWeight;
            primaryDirectLighting += directBase;
            float directWeight = luminance(directBase);
            shadowVisibilityNumerator += directWeight * shadowMask;
            shadowVisibilityDenominator += directWeight;
        }
    }

    lo += evaluateEnvironmentDiffuse(worldNormal, pixelToView, pbr);

    if (isPrimarySurface) {
        payLoad.primaryDirectLighting = primaryDirectLighting;
        payLoad.primaryShadowVisibility = shadowVisibilityDenominator > 0.0
            ? clamp(shadowVisibilityNumerator / shadowVisibilityDenominator, 0.0, 1.0)
            : 1.0;
    }

    if (payLoad.isBouncing) {
        lo = (lo + pbr.emissive) * pbr.opacity;
        lo *= payLoad.throughput;
        payLoad.hitValue += lo;
    } else {
        payLoad.opacity += (1.0 - payLoad.opacity) * pbr.opacity;
    }

    int maxBouncesForSurface = MAX_BOUNCE_COUNT;
    if (payLoad.bounceCount < maxBouncesForSurface)
    {
        float tMin = 0.001;
        float tMax = 1000.0;
        vec3 f0 = baseReflectivity(pbr.albedo, pbr.metallic);
        vec3 bounceWeight = min(fresnelSchlickFunction(max(dot(pixelToView, worldNormal), 0.0), f0), vec3(0.98));
        float bounceEnergy = maxComponent(payLoad.throughput * bounceWeight);
        if (bounceEnergy > MinimumBounceThroughput) {
            vec3 baseHitValue = payLoad.hitValue;
            vec3 parentThroughput = payLoad.throughput;
            float parentOpacity = payLoad.opacity;
            float parentAccumulatedDistance = payLoad.accumulatedDistance;
            int parentBounceCount = payLoad.bounceCount;
            bool parentIsBouncing = payLoad.isBouncing;

            vec3 perfectReflection = safeNormalize(reflect(-pixelToView, worldNormal), worldNormal);
            float rayTraceWeight = 1.0 - smoothstep(ReflectionRayFadeStart, ReflectionRayFadeEnd, pbr.roughness);
            float environmentWeight = 1.0 - rayTraceWeight;
            vec3 combinedReflectionContribution = vec3(0.0);

            if (environmentWeight > 0.001) {
                vec3 stableEnvironmentReflection = parentThroughput * bounceWeight * sampleStableGlossyEnvironment(perfectReflection, pbr.roughness);
                combinedReflectionContribution += clampReflectionContribution(stableEnvironmentReflection) * environmentWeight;
            }

            if (rayTraceWeight > 0.001) {
                int sampleCount = reflectionSampleCount(parentBounceCount);
                vec3 reflectionContribution = vec3(0.0);
                int validReflectionSamples = 0;

                for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                    float sampleSeed = stableReflectionSeed(worldPos, sampleIndex, parentBounceCount);
                    vec3 bounceDirection = buildReflectionDirection(worldNormal, pixelToView, pbr.roughness, sampleSeed);
                    if (dot(bounceDirection, worldNormal) <= 0.0001) {
                        continue;
                    }

                    payLoad.bounceCount = parentBounceCount + 1;
                    payLoad.isBouncing = true;
                    payLoad.opacity = 0.0;
                    payLoad.throughput = parentThroughput * bounceWeight;
                    payLoad.accumulatedDistance = parentAccumulatedDistance;
                    vec3 bounceOrigin = offsetRayOrigin(worldPos, worldNormal, bounceDirection, PrimaryRayBias);
                    traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, DEFAULT_RENDER_LAYER_MASK, 0, 0, 0, bounceOrigin, tMin, bounceDirection, tMax, 0);

                    reflectionContribution += clampReflectionContribution(payLoad.hitValue - baseHitValue);
                    payLoad.hitValue = baseHitValue;
                    payLoad.opacity = parentOpacity;
                    payLoad.accumulatedDistance = parentAccumulatedDistance;
                    payLoad.bounceCount = parentBounceCount;
                    payLoad.isBouncing = parentIsBouncing;
                    payLoad.throughput = parentThroughput;
                    validReflectionSamples++;
                }

                if (validReflectionSamples > 0) {
                    combinedReflectionContribution += (reflectionContribution / float(validReflectionSamples)) * rayTraceWeight;
                }
            }

            payLoad.hitValue = baseHitValue + combinedReflectionContribution;
            payLoad.opacity = parentOpacity;
            payLoad.accumulatedDistance = parentAccumulatedDistance;
            payLoad.bounceCount = parentBounceCount;
            payLoad.isBouncing = parentIsBouncing;
            payLoad.throughput = parentThroughput;
        }
    }

    if (pbr.opacity < 0.99 && payLoad.opacity < 0.99) {
        float tMin = 0.001;
        float tMax = 1000.0;
        vec3 origin = offsetRayOrigin(worldPos, worldNormal, gl_WorldRayDirectionEXT, PrimaryRayBias) + gl_WorldRayDirectionEXT * PrimaryRayBias;
        vec3 direction = gl_WorldRayDirectionEXT;
        payLoad.isBouncing = false;
        traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, DEFAULT_RENDER_LAYER_MASK, 0, 0, 0, origin, tMin, direction, tMax, 0);
    }

    payLoad.recursionDepth--;
}