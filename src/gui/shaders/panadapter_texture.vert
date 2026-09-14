#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 textureCoordinate;
layout(location = 0) out vec2 vertexTextureCoordinate;

layout(std140, binding = 0) uniform Transform
{
    mat4 matrix;
    float rowOffset;
};

void main()
{
    vertexTextureCoordinate = vec2(textureCoordinate.x, fract(textureCoordinate.y + rowOffset));
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
