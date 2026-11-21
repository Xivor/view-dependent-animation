#version 460 core

// Unlit fragment shader for rigged_mesh.vert: shows the albedo texture (if the mesh has one)
// or objectColor, with no lights, no shading, no tone mapping and no gamma.
// It still writes the mesh-id and normal buffers because the hybrid pass has 3 color attachments
// that the post-processing passes read.

#include "hybrid_metadata.glsl"

in vec3 normalO;
in vec2 uvO;
in float realZ;

uniform uint meshId = 1;
uniform vec3 objectColor = vec3(0.8);   // set by Renderer.cpp from obj.material.color (use vec4 if that is a glm::vec4)
uniform int hasTexture = 0;
uniform sampler2D albedoTexture;

layout(location = 0) out vec4 FragColor0;  // scene
layout(location = 1) out uvec4 FragColor1; // meshId
// layout(location = 2) out vec4 FragColor2;  // normal buffer

void main() {
    FragColor0 = vec4(hasTexture == 1 ? texture(albedoTexture, uvO).rgb : objectColor, 1.0);
    FragColor1 = getFragColor1(meshId);
    // FragColor2 = getFragColor2(normalO, realZ);
}
