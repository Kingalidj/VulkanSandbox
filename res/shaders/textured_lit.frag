#version 450

//shader input
layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 texCoord;

//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 2, binding = 0) uniform sampler2D tex1;

void main()
{
	//outFragColor = vec4(1.0, 1.0, 0.0, 1.0);
	vec4 color = texture(tex1,texCoord);
	if (color.w == 0) { discard; }
	outFragColor = color;
}

