#version 460 core

layout(location = 0) out vec4 FragColor0; // scene color
layout(location = 1) out vec4 FragColor1; // metadata
layout(location = 2) out vec4 FragColor2; // normals

uniform vec3 jointColor;
uniform uint jointId;

void main() {
    FragColor0 = vec4(jointColor, 1.0);
    FragColor1 = uvec4(0, 0, jointId + 1, 0);
    FragColor2 = vec4(0., 0., 1., 0.);
}
