// deko3d 2D fragment shader
// 输入: 顶点插值颜色
// 输出: 最终像素颜色

#version 450

layout(location=0) in vec4 v_color;
layout(location=0) out vec4 o_color;

void main() {
    o_color = v_color;
}
