# HeartRate-Monitor-STM32

基于 **STM32F103RCT6 + PulseSensor + ST7735S LCD** 的心率监测原型，使用 Keil MDK 和 STM32F10x 标准外设库开发。

系统采集传感器的模拟信号，计算心率（BPM），在 LCD 上显示测量值与报警阈值，并在读数超过阈值时驱动蜂鸣器。

## 项目状态

仓库包含固件、原理图、嘉立创 EDA 工程（含 PCB）和自动化 C 回归测试。

当前版本已修复旧读数不失效、已复现的较低次峰重复计数及主循环延迟改变采样时序的问题：采用 **TIM3 → ADC1 → DMA** 连续采集、带时间戳的样本队列、迟滞检测与独立的有效读数过期检查。共享计时改为中断独占写入的 `volatile` 毫秒计数。

**验证结果**：固件共用 C 源码通过 535,808 条断言；30～200 BPM 的全部 171 个整数点稳态平均误差均小于 1 BPM；本机 Keil 重构建为 0 错误、0 警告并生成 HEX。模型和编译验证不能替代真实佩戴与电路测试；详见[验证记录](docs/VALIDATION.md)和[当前边界](#当前边界与后续工作)。

## 已实现功能

| 功能 | 当前行为 |
| --- | --- |
| 定时采集 | TIM3 以 1kHz 触发 PC3 / ADC1_CH13，DMA 循环接收 |
| 样本滤波 | 每 10 个原始样本去掉一个最大、最小值，对其余 8 点平均，每 10ms 产生一个算法样本 |
| 心率计算 | 双阈值迟滞、候选沿间隔校验、最近最多 4 个有效间隔平均；接受 30～200 BPM |
| 读数失效 | 超过 2.5s 没有有效更新归零；采样中断、队列溢出或弱信号也会清除读数 |
| LCD 显示 | SPI2 驱动 ST7735S，128×160、RGB565；显示 BPM 和 Limit |
| 阈值调节 | 默认 100 BPM；步进 5；范围 40～180 BPM；重启恢复默认值 |
| 高心率报警 | `BPM > BPM_Threshold` 时 PA6 输出高电平；相等或 BPM 为 0 时不响 |
| 运行指示 | PC0 在界面刷新时翻转，目标间隔 200ms；当前不随心搏闪烁 |

原理图提供 TF 卡、红外和串口扩展资源，当前固件未实现相应业务功能。当前只有高心率阈值报警，没有低心率报警。

## 快速开始

1. 按下方表格连接传感器、LCD 和下载器，并确认共地。
2. 按[编译与烧录](#编译与烧录)准备环境，打开 Keil 工程并 Rebuild。
3. 烧录 HEX，复位进入用户程序。
4. 屏幕应显示“心率检测器”、`BPM` 和 `Limit`。初始 BPM 为 0。
5. 稳定放置手指：算法先预热 250 个样本（约 2.5s），再取得至少两个连续有效间隔才发布读数。低心率下等待更久，不能保证首次出数在 3～5s 内。
6. 按住 KEY1 或 KEY2 至少约 50ms 后松开，分别增加或减少阈值。当前为松手触发，没有长按连调。

`BPM = 0` 表示没有可用读数，不表示测得实际心率为零。失去信号后的归零有检测延迟；当前采用最近最多 4 个间隔平滑，真实心率突变时显示也有响应延迟。

## 硬件与接线

### PulseSensor

| 传感器引脚 | STM32 连接 | 说明 |
| --- | --- | --- |
| S / 信号 | PC3 | ADC1 通道 13；仓库 PCB 经 H4 第 7 脚引出 |
| + / 电源 | 3V3 | 3.3V 供电 |
| − / 地 | GND | 与主控共地 |

传感器通过通用 GPIO 排针接入。原理图中的 U15 是红外接口，PulseSensor 信号应接 PC3。

### LCD（ST7735S）

| LCD 引脚 | STM32 引脚 | 功能 |
| --- | --- | --- |
| VCC | 3V3 | 电源 |
| GND | GND | 地 |
| SCL / SCK | PB13 | SPI2 时钟 |
| SDA / MOSI | PB15 | SPI2 数据输出 |
| CS | PB10 | 片选 |
| DC | PB11 | 数据/命令选择 |
| RES | PB12 | 复位 |
| BL | PC6 | 仓库电路的背光控制信号 |

仓库 LCD 接口含背光三极管驱动。使用其他成品模块时，应按实际接口定义连接电源与背光。

### 按键、蜂鸣器与 LED

| 引脚 | 用途 | 电平与行为 |
| --- | --- | --- |
| PA0 | KEY1：阈值增加 | 高电平有效，原理图有外部下拉 |
| PB8 | KEY2：阈值减少 | 低电平有效，原理图有外部上拉 |
| PA6 | 蜂鸣器控制 | 高电平使 S8050 导通；固件按有源蜂鸣器的开关方式驱动 |
| PC0 | LED1：运行指示 | 低电平点亮，目标每 200ms 翻转 |
| PC1 / PC2 | LED2 / LED3 | 初始化后保持熄灭，当前主程序未使用 |

按键为浮空输入，依赖原理图的外部上下拉；移植到其他板时需提供相同稳定电平。SW3/SW4 与 SW6/SW7 分别连接同一 KEY1/KEY2 网络，是两组物理按键、两个逻辑功能。

### 定时与 DMA 资源

- TIM3：更新事件 TRGO，1kHz；ADC1 规则通道外部触发。
- DMA1 Channel 1：循环缓冲 20 个 16 位样本，半传输/全传输各发布一个 10 点块。
- SysTick：1ms 单调计数，用于初始化延时、界面/按键调度及采样停止检查。
- 中断优先级：SysTick 为 0、DMA1 Channel 1 为 1；DMA ISR 只滤波和入队，算法在主循环运行。

移植或增加其他模块时应避免占用上述资源。采样独立于 LCD 刷新；界面和按键仍采用主循环调度，LCD 初始化及 SPI 传输存在同步等待。

## 编译与烧录

### 开发环境

| 项目 | 工程声明 | 本次实际验证 |
| --- | --- | --- |
| IDE | Keil MDK-ARM / µVision5 | 本机 µVision |
| 编译器 | ARM Compiler 6.23 | ARM Compiler 6.13.1 |
| 器件包 | Keil.STM32F1xx_DFP 2.4.1 | 使用本机安装的器件支持 |
| CMSIS CORE | ARM.CMSIS 6.1.0 | ARM.CMSIS 5.6.0 |
| 外设库 | 仓库内 STM32F10x 标准外设库 V3.5 | 同左，NVIC 优先级设置采用 CMSIS API |
| 芯片 / 宏 | STM32F103RC；`STM32F10X_HD`、`USE_STDPERIPH_DRIVER` | 同左 |
| 时钟 | 外部 8MHz 晶振，目标系统时钟 72MHz | 静态配置核对；未测量实物时钟 |

编译器和 CMSIS 的实际选用版本可能与工程声明不同，应在构建日志和依赖路径中核对。原先 CMSIS 5 下的 `NVIC_Type` 缺少 `IPR` 错误已通过 `NVIC_SetPriority()` 兼容处理；本次记录验证的是 6.13.1 / 5.6.0 组合，未将工程声明组合记作已通过。

### 构建

```bash
git clone https://github.com/xiangwen-ctrl/HeartRate-Monitor-STM32.git
cd HeartRate-Monitor-STM32
```

1. 安装可用的 ARM Compiler、器件包和 CMSIS；可参考上表的已验证组合。
2. 打开 `software/stm32/project/HeartRateMonitor.uvprojx`。
3. 核对目标 `HeartRateMonitor`、器件 STM32F103RC 和实际编译器/CMSIS 版本。
4. 执行 **Rebuild all target files**，确认编译、链接成功。
5. 默认输出：`software/stm32/project/Objects/HeartRateMonitor.hex`。

本次构建：**0 Error(s), 0 Warning(s)**；Code=7620、RO-data=4520、RW-data=8、ZI-data=4952 字节。详见[验证记录](docs/VALIDATION.md)。

### 烧录

- **ST-Link / SWD**：通过 H1 连接 SWDIO（PA13）、SWCLK（PA14）、GND，并按下载器要求连接目标电压参考。工程关闭 JTAG、保留 SWD。
- **CH340 串口下载**：原理图连接 USART1（PA9 / PA10）并提供 BOOT0、复位控制电路；使用支持 STM32 串口下载的工具下载，随后确认从用户 Flash 启动。

## 自动化验证

在仓库根目录运行：

```bash
python -X utf8 verify/run_native_tests.py
```

需要 Python 3，以及 GCC/Clang 或 Windows 上的 Visual Studio C++ Build Tools（安装 C++ 工具组件）。脚本会自动定位可用编译器；Linux 也可运行 `python3 verify/run_native_tests.py`。不依赖第三方 Python 包。

测试编译并运行固件共用的 `pulse_detect.c`、`pulse_buffer.c`，失败返回非零退出码。GitHub Actions 在 push / pull request 时运行同一命令。

| 覆盖项 | 本次结果 |
| --- | --- |
| 30～200 BPM 共 171 个整数点，每点 60s | 1kHz 合成原始信号经过 10 点滤波，后半段全部有输出，最差平均误差 0.424 BPM |
| 60 → 240 BPM | 无效候选沿不能无限保留旧读数，切换后 3s 的检查起点已归零 |
| 每秒一周期、400/300 幅度双峰 | 无噪声及轻微噪声时均稳定输出 60 BPM |
| 信号丢失、停止采样、恢复 | 清除失效读数，恢复后重新取得有效读数 |
| 11ms / 不规则时间戳、32 位计时回绕 | 没有固定 10ms 换算偏差，样本缺口后重新预热 |
| 去极值滤波、队列顺序/回绕/溢出、150ms 前台暂停 | 通过断言；积压处理使用采集时间，溢出报告数据丢失 |

`verify/bpm_fix_verify.py` 保留为**历史算法对比**，其 W=250 Python 实现不再代表当前固件；当前验证以 C 回归脚本为准。算法参数及状态机见[算法说明](docs/ALGORITHM.md)。

## 当前边界与后续工作

- 高触发阈值为窗口幅度的 80%，低复位阈值为 50%，能抑制已复现的较低次峰；**等高或接近主峰的双峰、强运动伪影仍可能误检**。不能据此宣称所有双峰已解决或所有未佩戴输入均无误报。
- 250 点窗口在首次填满后，每 25 点更新一次极值和阈值；幅度和基线突变需要适应时间。
- 队列有效容量为 31 块（约 310ms）；溢出、DMA 错误或 ISR 错过两个缓冲边界时清除历史并重新预热。系统没有补造丢失样本。
- 本次完成了主机 C 测试和 ARM 固件编译，尚未完成实物烧录、采样间隔与中断延迟测量、运动干扰、供电及参考心率仪对照测试。
- 后续可补充波形输出、信号质量提示、实物照片和测量记录。

## 常见问题

| 现象 | 排查方向 |
| --- | --- |
| BPM 一直为 0 | 检查 PC3、3.3V、共地；等待预热及两个有效间隔；检查信号幅度、TIM3/ADC1/DMA 中断及队列是否丢样 |
| 手指移开后仍短暂显示数字 | 失效检查有延迟；若持续误报，记录原始波形，区分真实脉搏与伪影 |
| 读数跳动或偏高 | 检查接触、强次峰、基线漂移或干扰，记录 ADC 波形和采样时间 |
| 按键没有立即响应 | 按住至少约 50ms 再松开；检查 PA0 高有效、PB8 低有效与外部上下拉；显示耗时也会影响按键扫描 |
| 蜂鸣器不响 | 确认 BPM 严格大于 Limit、PA6 高电平及三极管接线；无源蜂鸣器需额外 PWM 驱动 |
| LCD 无显示 | 检查供电、背光、SPI2、控制引脚及模块与 ST7735S 初始化/偏移的匹配 |
| 仍报 `NVIC_Type` 缺少 `IPR` | 确认使用更新后的 `misc.c`，执行完整 Rebuild 并核对 CMSIS 引用路径 |

## 项目结构与硬件资料

```text
HeartRate-Monitor-STM32/
├── .github/workflows/verify.yml        # 自动运行 C 回归测试
├── README.md
├── LICENSE
├── docs/
│   ├── ALGORITHM.md                   # 状态机与采样实现
│   └── VALIDATION.md                  # 验证环境、结果与边界
├── hardware/
│   ├── schematic.pdf
│   ├── BOM.csv
│   └── 心率.eprj2                     # 含原理图与 PCB
├── software/stm32/
│   ├── project/HeartRateMonitor.uvprojx
│   ├── startup/
│   ├── STM32F10x_StdPeriph_Driver/
│   └── user/
│       ├── main.c                     # 读数/报警、按键、界面
│       └── api/
│           ├── adc.c/h                # TIM3、ADC、DMA 与队列集成
│           ├── pulse_detect.c/h       # 可在主机测试的心率状态机
│           ├── pulse_buffer.c/h       # 10 点滤波与采样块队列
│           ├── delay.c/h              # 单调毫秒计数与初始化延时
│           └── ...                    # LCD、SPI、LED、按键与蜂鸣器
└── verify/
    ├── run_native_tests.py             # 编译并执行 C 测试
    ├── test_pulse.c                    # 断言式回归用例
    └── bpm_fix_verify.py               # 历史 Python 算法对比
```

- [原理图 PDF](hardware/schematic.pdf)
- [嘉立创 EDA 工程](hardware/心率.eprj2)：含 PCB 布局布线；仓库尚未提供 ERC/DRC 结果及独立 Gerber、钻孔文件。
- [BOM](hardware/BOM.csv)：实际为 Tab 分隔的汇总表，缺少位号和部分关键器件型号；采购、装配前应从 EDA 重新核对并导出。

## 修复记录

- 采样改为 TIM3 触发 ADC + DMA，LCD 刷新不再决定采样节拍。
- 增加采集时间戳、队列溢出处理和采样停止检测。
- 有效读数过期独立于候选上升沿，持续无效输入不能刷新有效读数寿命。
- 加入 80% / 50% 迟滞阈值、有效间隔校验、连续间隔确认与平滑，覆盖已复现双峰。
- 计时改为 ISR 独占写入的 `volatile` 毫秒计数，主循环只读取，按无符号差值处理回绕。
- `misc.c` 通过 CMSIS API 设置优先级，消除对 `IP` / `IPR` 成员名称的依赖。
- 保留早期窗口就绪判断、低心率间隔计数、蜂鸣器初始化与电平修复。

## 许可

项目采用 [MIT License](LICENSE)。第三方代码保留各自原有版权与许可声明。
