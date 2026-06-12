// deko3d 2D vertex shader — 屏幕坐标转 NDC + 亮度控制
#version 450

layout(location=0) in vec2 a_position;
layout(location=1) in vec4 a_color;
layout(location=0) out vec4 v_color;

layout(std140, binding=0) uniform UboData {
    vec2 u_screen_size;
    float u_brightness;
} ubo;

void main() {
    float ndc_x = (a_position.x / ubo.u_screen_size.x) * 2.0 - 1.0;
    float ndc_y = 1.0 - (a_position.y / ubo.u_screen_size.y) * 2.0;
    gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
    v_color = a_color * ubo.u_brightness;
}
