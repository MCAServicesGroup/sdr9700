#version 440

layout(location = 0) in vec2 vertexTextureCoordinate;
layout(location = 0) out vec4 fragmentColor;
layout(binding = 1) uniform sampler2D sourceTexture;

void main()
{
    fragmentColor = texture(sourceTexture, vertexTextureCoordinate);
}
