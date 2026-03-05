/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */

const float RANDOM_PI = 3.14159265359;

// Generate a random unsigned int from two unsigned int values, using 16 pairs
// of rounds of the Tiny Encryption Algorithm. See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
uint tea(uint val0, uint val1)
{
    uint v0 = val0;
    uint v1 = val1;
    uint s0 = 0;

    for (uint n = 0; n < 16; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }

    return v0;
}

// Generate a random unsigned int in [0, 2^24) given the previous RNG state
// using the Numerical Recipes linear congruential generator
uint lcg(inout uint prev)
{
    uint LCG_A = 1664525u;
    uint LCG_C = 1013904223u;
    prev = (LCG_A * prev + LCG_C);
    return prev & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float rnd(inout uint prev)
{
    return (float(lcg(prev)) / float(0x01000000));
}

float rnd(float seed)
{
    return fract(sin(seed) * 43758.5453);
}

float rnd(float seed, float salt)
{
    return fract(sin(seed * 12.9898 + salt * 78.233) * 43758.5453);
}

mat3 orthonormalBasis(vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return mat3(tangent, bitangent, normal);
}

vec3 sampleConeVector(vec3 direction, float coneAngle, float seed) {
    vec3 safeDirection = normalize(direction);
    float u = rnd(seed, 17.0);
    float v = rnd(seed, 59.0);
    float cosTheta = mix(cos(coneAngle), 1.0, u);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 2.0 * RANDOM_PI * v;
    vec3 localDirection = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(orthonormalBasis(safeDirection) * localDirection);
}

vec3 sampleGGXReflection(vec3 normal, vec3 viewDirection, float roughness, float seed) {
    vec3 safeNormal = normalize(normal);
    vec3 safeViewDirection = normalize(viewDirection);
    mat3 basis = orthonormalBasis(safeNormal);
    vec3 localView = normalize(transpose(basis) * safeViewDirection);
    localView.z = max(localView.z, 0.0001);

    // Heitz GGX VNDF sampling. The alpha mapping matches the direct GGX BRDF: alpha = roughness^2.
    float alpha = max(roughness * roughness, 0.001);
    vec3 stretchedView = normalize(vec3(alpha * localView.x, alpha * localView.y, localView.z));

    vec3 tangent1 = stretchedView.z < 0.999
        ? normalize(cross(vec3(0.0, 0.0, 1.0), stretchedView))
        : vec3(1.0, 0.0, 0.0);
    vec3 tangent2 = cross(stretchedView, tangent1);

    float u1 = rnd(seed, 19.0);
    float u2 = rnd(seed, 73.0);
    float radius = sqrt(u1);
    float phi = 2.0 * RANDOM_PI * u2;
    float t1 = radius * cos(phi);
    float t2 = radius * sin(phi);
    float blend = 0.5 * (1.0 + stretchedView.z);
    t2 = mix(sqrt(max(1.0 - t1 * t1, 0.0)), t2, blend);

    vec3 stretchedHalfVector = t1 * tangent1 + t2 * tangent2 + sqrt(max(1.0 - t1 * t1 - t2 * t2, 0.0)) * stretchedView;
    vec3 localHalfVector = normalize(vec3(alpha * stretchedHalfVector.x, alpha * stretchedHalfVector.y, max(stretchedHalfVector.z, 0.0)));
    vec3 halfVector = normalize(basis * localHalfVector);
    return normalize(reflect(-safeViewDirection, halfVector));
}

vec3 randomHemisphereVector(vec3 normal, float seed) {
    float u = rnd(seed, 11.0);
    float v = rnd(seed, 31.0);
    float phi = 2.0 * RANDOM_PI * u;
    float cosTheta = v;
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 localDirection = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(orthonormalBasis(normalize(normal)) * localDirection);
}