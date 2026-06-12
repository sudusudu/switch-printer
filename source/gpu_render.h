#pragma once
#include <switch.h>
// GPU framebuffer 2D renderer (no deko3d needed)

// ============================================================
// deko3d GPU 2D 渲染器 + 触摸交互
// - 设备初始化、framebuffer、命令缓冲
// - 基础图元：矩形、圆环、文本、进度条
// - 触摸坐标读取 + 按钮区域碰撞检测
// ============================================================

// RGBA 颜色 (0.0~1.0)
typedef struct { float r, g, b, a; } GpuColor;

// 矩形区域（用于按钮布局和触摸碰撞检测）
typedef struct {
    float x, y, w, h;
    int   id;        // 按钮 ID
    const char *label;
} GpuRect;

// 预定义颜色
#define GPU_WHITE   ((GpuColor){1.0f,1.0f,1.0f,1.0f})
#define GPU_BLACK   ((GpuColor){0.0f,0.0f,0.0f,1.0f})
#define GPU_RED     ((GpuColor){0.88f,0.25f,0.25f,1.0f})
#define GPU_GREEN   ((GpuColor){0.25f,0.78f,0.38f,1.0f})
#define GPU_BLUE    ((GpuColor){0.16f,0.44f,0.97f,1.0f})
#define GPU_YELLOW  ((GpuColor){0.94f,0.63f,0.25f,1.0f})
#define GPU_CYAN    ((GpuColor){0.25f,0.50f,0.78f,1.0f})
#define GPU_DIM     ((GpuColor){0.36f,0.39f,0.44f,1.0f})
#define GPU_BG      ((GpuColor){0.04f,0.05f,0.08f,1.0f})
#define GPU_CARD    ((GpuColor){0.08f,0.09f,0.15f,1.0f})
#define GPU_BORDER  ((GpuColor){0.12f,0.13f,0.20f,1.0f})
#define GPU_WARN    ((GpuColor){0.88f,0.75f,0.25f,1.0f})
#define GPU_OK      ((GpuColor){0.25f,0.78f,0.38f,1.0f})
#define GPU_HOT     ((GpuColor){0.88f,0.25f,0.25f,1.0f})

// 初始化 GPU 渲染器
Result gpu_init(void);

// 退出时释放 GPU 资源
void   gpu_exit(void);

// ---- 帧控制 ----
void   gpu_begin_frame(float brightness, int burnin_dx, int burnin_dy);
void   gpu_end_frame(void);

// ---- 基础图元 ----
void   gpu_draw_rect(float x, float y, float w, float h, GpuColor c);
void   gpu_draw_rect_rounded(float x, float y, float w, float h, float r, GpuColor c);
void   gpu_draw_rect_outline(float x, float y, float w, float h, GpuColor c);
void   gpu_draw_ring(float cx, float cy, float radius, float thickness,
                     float ratio, GpuColor c);
void   gpu_draw_progress_bar(float x, float y, float w, float h,
                             float ratio, GpuColor c);
void   gpu_draw_text(float x, float y, float size, const char *text, GpuColor c);
void   gpu_draw_text_centered(float x, float y, float w, float size,
                              const char *text, GpuColor c);

// ---- 触摸 ----
// 返回 true 表示有触摸，填充坐标 (0,0)~(1280,720)
bool   gpu_get_touch(float *tx, float *ty);
// 碰撞检测：返回第一个命中的按钮 ID，-1 表示未命中
int    gpu_hit_test(float tx, float ty, const GpuRect *rects, int count);
// 检测是否有新触摸按下（边缘触发，用于按钮点击）
bool   gpu_touch_pressed(void);

// ---- 工具 ----
void   gpu_rgba_to_hsv(GpuColor c, float *h, float *s, float *v);
GpuColor gpu_hsv_to_rgba(float h, float s, float v);
GpuColor gpu_temp_color(float temp_c);
