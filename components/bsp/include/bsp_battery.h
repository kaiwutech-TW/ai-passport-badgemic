// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// BSP 写入自定义电池 profile,芯片直接给 SOC%,无需外部分压电阻与查表。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 将 CW2017 CONFIG 写为睡眠值并回读确认，失败时重试一次。这是 deep
// sleep 前的终端操作；芯片未初始化时视为无需处理并返回成功。
esp_err_t bsp_battery_sleep(void);

// 运行期读数不可信时(例如电压正常却读到 0%)重新接管电量计:记录 CONFIG 与
// UPDATE_FLAG,profile 丢失则重写,已处于正常态则重启一次 SOC 计算，然后等待就绪。
// 最长阻塞约 5 s(就绪等待窗口),调用方应限频。未初始化时等同 bsp_battery_init()。
esp_err_t bsp_battery_recover(void);

// 剩余电量百分比 0..100;读失败返回 -1。
int bsp_battery_soc(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);
