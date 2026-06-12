#pragma once
#include <switch.h>

// ============================================================
// 电源管理 + Applet 生命周期
// - 打印期间禁止系统休眠
// - 屏幕超时降亮度 + OLED 防烧屏像素微移
// - HOME 键检测 → 硬暂停打印
// ============================================================

// 初始化电源管理（必须在 appletMainLoop 就绪后调用）
Result power_init(void);

// 退出时恢复系统默认行为
void   power_exit(void);

// 打印开始时调用：禁止休眠
void   power_print_start(void);

// 打印结束/取消时调用：恢复休眠允许
void   power_print_end(void);

// 每帧调用：检查空闲超时降亮度、防烧屏微移
// 返回 true 表示当前处于低亮度模式
bool   power_update(u64 dt_ns);

// 检测 applet 焦点变化（HOME键/切桌面）
// 返回 true 表示刚失去焦点 → 调用者应暂停打印
// 返回 false 表示焦点正常或刚恢复 → 调用者可恢复
typedef enum {
    FOCUS_OK,
    FOCUS_LOST,    // 刚失去焦点 → 暂停打印
    FOCUS_RETURNED // 刚恢复焦点 → 可恢复打印
} FocusEvent;
FocusEvent power_check_focus(void);

// 获取当前亮度乘数 (0.0~1.0)，供 GPU UI 应用
float  power_get_brightness(void);

// 获取防烧屏像素偏移 (dx, dy)，供 GPU UI 应用
void   power_get_burnin_offset(int *dx, int *dy);

// 用户交互时调用：重置空闲计时器，恢复亮度
void   power_on_interaction(void);
