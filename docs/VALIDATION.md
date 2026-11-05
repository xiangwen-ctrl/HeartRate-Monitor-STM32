# 验证记录

验证日期：2026-11-05。内容对应本次心率检测、定时采样和计时修复；不包含实物运行结论。

## 原生 C 回归

命令：

```bash
python -X utf8 verify/run_native_tests.py
```

本次使用 Windows 上的 Visual Studio C++ 编译器，启用 `/W4 /WX /O2 /std:c11`，将以下文件编译到同一主机程序：

- `verify/test_pulse.c`
- `software/stm32/user/api/pulse_detect.c`
- `software/stm32/user/api/pulse_buffer.c`

无第三方 Python 依赖。Linux 上的 GCC/Clang 路径使用 C99、`-O2 -Wall -Wextra -Werror`；仓库提供 GitHub Actions 运行入口。

```text
PASS: all 171 integer BPMs, max steady mean error 0.424 BPM
PASS: sustained 240 BPM expires, contact loss and recovery, stopped ADC
PASS: reported 400/300-amplitude double peak stays at 60 BPM, with/without noise
PASS: 11ms sampling, irregular timestamps, uint32 wrap, missing samples
PASS: DMA trim filter, queue order/wrap/overflow, 150ms foreground pauses
PASS: 535808 assertions against firmware C sources
```

171 点扫描模拟每点 60s，统计后 30s；合成原始 ADC 信号以 1kHz 生成，每 10 点经过固件共用去极值平均再送入检测器。除数值范围和均值外，还要求足够多次有效心搏更新，因此不能只靠保持旧 BPM 通过。

双峰、超范围、采样停止、样本缺口、时间回绕和队列溢出均有独立断言。平均误差是模型下的稳态统计，不是实物精度或最大瞬时误差。

## ARM 固件重构建

- 工程：`software/stm32/project/HeartRateMonitor.uvprojx`
- 目标：`HeartRateMonitor` / STM32F103RC
- 实际编译器：ARM Compiler 6.13.1
- 实际 CMSIS：5.6.0
- 工程仍声明 Compiler 6.23、CMSIS 6.1.0；本次实际验证版本与声明版本在 README 中分别标注。

```text
linking...
Program Size: Code=7620 RO-data=4520 RW-data=8 ZI-data=4952
FromELF: creating hex file...
HeartRateMonitor.axf - 0 Error(s), 0 Warning(s).
```

生成 `Objects/HeartRateMonitor.axf` 和 `Objects/HeartRateMonitor.hex`。编译产物仍由 `.gitignore` 排除，不纳入源码提交。

`misc.c` 以 `NVIC_SetPriority()` 替代直接访问 `IPR`，处理 CMSIS 5/6 的结构成员名称差异；新增的算法和缓冲 C 文件已加入 Keil 工程。

## 修复与验证的对应关系

| 原问题 | 修改 | 证据 |
| --- | --- | --- |
| 无效上升沿持续推迟超时 | `last_valid_ms` 与候选沿时间分离 | 60 → 240 后旧读数清零、恢复测试 |
| 已复现双峰重复计数 | 80% / 50% 双阈值、连续间隔确认 | 指定 400/300 双峰在无噪声和轻微噪声下均输出 60 |
| LCD 延迟影响采样与 IBI | TIM3 触发 ADC + DMA、块时间戳与队列 | ARM 构建；前台暂停和真实时间戳模型断言 |
| 主循环清零 ISR 共享计数 | 私有 `volatile` 毫秒计数，前台只读 | 源码检查；32 位时间回绕测试 |
| 采样历史丢失后继续出数 | 队列故障返回、清检测器、重新预热 | 队列溢出与样本缺口断言 |
| CMSIS 成员兼容错误 | 使用 CMSIS 优先级 API | 本机完整 Rebuild 0 错误、0 警告 |

## 尚未验证

本次没有连接开发板；尚未烧录或测量 TIM3/ADC 实际采样节拍、DMA ISR 延迟、LCD 与按键实物交互、报警电平、电源噪声、运动干扰及与参考心率设备的误差。

等高次峰和接近主峰的次峰超出了当前幅度阈值策略的保证范围。后续应保存真实 ADC 波形并扩充回归，不能据当前测试宣称所有输入都无误检。
