#version 460 core

// Paints the skybox black. Mesh id and normal buffer get the same values the pass is cleared to,
// so the post-processing passes see "nothing" there.

layout(location = 0) out vec4 FragColor0;
layout(location = 1) out uvec4 FragColor1;
layout(location = 2) out vec4 FragColor2;

void main() {
    FragColor0 = vec4(0.0, 0.0, 0.0, 1.0);
    FragColor1 = uvec4(0);
    FragColor2 = vec4(0.0);
}
