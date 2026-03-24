const float PI = 3.141592;
const float MIN_FLOAT_VALUE = 0.0001f;

//Reference from: https://rtarun9.github.io/blogs/physically_based_rendering/

float normalDistributionFunction(const vec3 normal, const vec3 halfWayVector, float roughness) {
    float alpha = max(roughness * roughness, MIN_FLOAT_VALUE);
    float alpha2 = alpha * alpha;
    float NdotH = clamp(dot(normal, halfWayVector), 0.0, 1.0);
    if (NdotH <= 0.0) {
        return 0.0;
    }
    return alpha2 / max(PI * pow(NdotH * NdotH * (alpha2 - 1.0) + 1.0, 2.0f), MIN_FLOAT_VALUE);
}

vec3 fresnelSchlickFunction(const float vDotH, const vec3 reflecvitivity) {
    return reflecvitivity + (1.0 - reflecvitivity) * pow(clamp(1.0 - vDotH, 0, 1), 5.0);
}

vec3 baseReflectivity(const vec3 albedo, const float metallicFactor)
{
    return mix(vec3(0.04, 0.04, 0.04), albedo, metallicFactor);
}

float schlickGGXGeometry(float nDotX, float roughnessFactor)
{
    float roughness = clamp(roughnessFactor, 0.02, 1.0);
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotX / (max((nDotX * (1.0f - k) + k), MIN_FLOAT_VALUE));
}

float smithGeometryFunction(const vec3 normal, const vec3 viewDirection, const vec3 lightDirection, const float roughnessFactor)
{
    float nDotV = clamp(dot(normal, viewDirection), 0, 1);
    float nDotL = clamp(dot(normal, lightDirection), 0, 1);
    return schlickGGXGeometry(nDotV, roughnessFactor) * schlickGGXGeometry(nDotL, roughnessFactor);
}

// BRDF = kD * diffuseBRDF + kS * specularBRDF. (Note : kS + kD = 1).
vec3 cookTorrenceBRDF(const vec3 normal, const vec3 viewDirection, const vec3 pixelToLightDirection, const vec3 albedo, const float roughnessFactor,
const float metallicFactor)
{
    const float roughness = clamp(roughnessFactor, 0.02, 1.0);
    const vec3 halfWayVector = normalize(viewDirection + pixelToLightDirection);
    const float nDotV = clamp(dot(normal, viewDirection), 0.0, 1.0);
    const float nDotL = clamp(dot(normal, pixelToLightDirection), 0.0, 1.0);
    if (nDotV <= 0.0 || nDotL <= 0.0) {
        return vec3(0.0);
    }

    const vec3 f0 = mix(vec3(0.04f, 0.04f, 0.04f), albedo.xyz, metallicFactor);

    // Using cook torrance BRDF for specular lighting.
    const vec3 fresnel = fresnelSchlickFunction(max(dot(viewDirection, halfWayVector), 0.0f), f0);

    const float normalDistribution = normalDistributionFunction(normal, halfWayVector, roughness);
    const float geometryFunction = smithGeometryFunction(normal, viewDirection, pixelToLightDirection, roughness);

    vec3 specularBRDF = (normalDistribution * geometryFunction * fresnel) /
    max(4.0f * nDotV * nDotL, MIN_FLOAT_VALUE);

    vec3 kS = fresnel;

    // Metals have kD as 0.0f, so more metallic a surface is, closes kS ~ 1 and kD ~ 0.
    // Using lambertian model for diffuse light now.
    vec3 kD = mix(vec3(1.0f, 1.0f, 1.0f) - fresnel, vec3(0.0f, 0.0f, 0.0f), metallicFactor);

    const vec3 diffuseBRDF = albedo / PI;

    return (kD * diffuseBRDF + kS * specularBRDF);
}
