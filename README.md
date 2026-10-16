# Accessible Instruments｜无障碍乐器控制系统

## 1. 项目简介｜Overview

Accessible Instruments 是一个面向无障碍音乐演奏的原型项目，探索将点头动作转化为机械击打，让使用脚踏乐器存在困难的使用者能够通过头部动作参与演奏。

Accessible Instruments is a prototype for accessible music performance. It explores converting head nods into mechanical strikes, allowing people who have difficulty using foot-operated instruments to participate through head movement.

项目结合姿态传感器、ESP32 控制器、舵机执行机构、可穿戴耳挂和定制打印结构，并提供浏览器屏幕模拟器，方便界面展示和交互测试。

The project combines a motion sensor, an ESP32 controller, a servo actuator, a wearable ear hook, and custom printed parts. A browser-based screen simulator supports interface demonstrations and interaction testing.

本项目目前属于原型开发阶段。软件模拟和几何检查不能代替实际佩戴、装配、演奏及耐久测试。

The project is currently a prototype. Software simulation and geometry checks do not replace physical fitting, assembly, performance, or durability testing.

## 2. 设计目标｜Design Goals

- 用点头动作提供一种替代脚踏的乐器控制方式。
- 通过现场归零和采样，适应不同佩戴姿态与点头幅度。
- 使用明确的触发和回位阈值，减少重复计数。
- 通过屏幕日志与状态反馈，帮助使用者理解系统行为。
- 使用模块化打印结构，方便安装、修改与迭代。

- Provide a head-operated alternative to foot-controlled instruments.
- Adapt to different wearing positions and nod amplitudes through zeroing and sampling.
- Reduce repeated counts through separate trigger and return thresholds.
- Make system behavior understandable through on-screen logs and status feedback.
- Use modular printed structures for installation, modification, and iteration.

## 3. 系统组成｜System Components

### 姿态输入｜Motion Input

WT9011DCL-BT50 通过蓝牙提供加速度数据。当前固件使用实时加速度向量与归零时记录的参考向量之间的夹角，估算姿态偏离程度。

The WT9011DCL-BT50 supplies acceleration data over Bluetooth. The current firmware estimates posture deviation from the angle between the live acceleration vector and the reference vector recorded during zeroing.

### 主控制器｜Main Controller

M5Stack Core2 / ESP32 负责蓝牙连接、数据接收、点头判断、屏幕显示、触摸输入和舵机控制。

The M5Stack Core2 / ESP32 handles Bluetooth communication, data reception, nod detection, screen rendering, touch input, and servo control.

### 执行机构｜Actuator

PCA9685 输出 PWM 信号驱动舵机。识别到有效点头后，舵机运动到击打位置，保持一段时间后返回静止位置。

The PCA9685 generates PWM signals for the servo. After a valid nod is detected, the servo moves to the strike position, holds briefly, and returns to rest.

### 机械结构｜Mechanical Structure

项目包含右耳挂、Type-C 机械定位插头、舵机固定架、接口法兰、盒体和盒盖。各零件提供 STEP 文件，便于在 CAD 软件中继续编辑。

The mechanical parts include a right ear hook, a Type-C mechanical locator, a servo bracket, an interface flange, an enclosure body, and a lid. STEP files are available for further CAD editing.

Type-C 定位插头为机械零件，不提供电气连接。

The Type-C locator is a mechanical part and does not provide an electrical connection.

### 浏览器模拟器｜Browser Simulator

模拟器以 320×240 坐标展示设备界面，提供 RESET、SAMPLE、姿态偏移输入、连接状态切换和点头演示。

The simulator displays the device interface in a 320×240 coordinate space. It provides RESET and SAMPLE controls, simulated posture input, connection toggling, and a nod demonstration.

模拟器用于界面展示和交互测试，不连接真实传感器或驱动实际舵机，也不是 ESP32 指令级仿真器。

The simulator supports interface demonstrations and interaction testing. It does not connect to a physical sensor, drive a real servo, or emulate ESP32 instructions.

## 4. 固件工作流程｜Firmware Workflow

### 第一步：连接传感器｜Step 1: Connect the Sensor

设备启动后初始化舵机驱动器，并扫描指定蓝牙传感器。连接成功且持续收到新数据时，姿态输入才被视为有效。

On startup, the device initializes the servo driver and scans for the configured Bluetooth sensor. Motion input is considered valid only when the connection is active and fresh data continues to arrive.

### 第二步：记录零位｜Step 2: Record the Zero Position

保持自然姿态，点击 RESET。固件将当前加速度向量记录为参考方向 g0，并清零点头计数。

Hold a natural posture and tap RESET. The firmware records the current acceleration vector as the reference direction, g0, and clears the nod count.

RESET 不会清零累计击打次数。

RESET does not clear the accumulated strike count.

### 第三步：采样点头幅度｜Step 3: Sample the Nod Amplitude

低头至希望用于演奏的幅度，再点击 SAMPLE。固件将当前偏离角度保存为参考幅度 ref。

Nod to the intended performance amplitude and tap SAMPLE. The firmware stores the current deviation angle as the reference amplitude, ref.

参考幅度至少为 2°。RESET 和 SAMPLE 均记录点击时的数据，不进行定时采样。

The reference amplitude must be at least 2°. RESET and SAMPLE record the values at the moment of activation; neither performs timed sampling.

### 第四步：回位并触发｜Step 4: Return and Trigger

采样后，先将头部返回参考幅度的 20% 以内。此时下一次点头获得触发资格。当偏离角度达到参考幅度的 80% 时，点头计数增加一次，并尝试触发击打。

After sampling, return the head to within 20% of the reference amplitude. This arms the next nod. When deviation reaches 80% of the reference amplitude, the nod count increases and the firmware attempts a strike.

再次触发前，必须先回到 20% 以内，避免持续停留在低头位置时重复计数。

Before another trigger, the head must return to within 20%. This prevents repeated counts while remaining in the lowered position.

### 第五步：击打与回位｜Step 5: Strike and Return

舵机收到击打指令后保持 300 ms，再返回静止位置。如果新的点头发生在舵机忙碌期间，点头仍会计数，但该次击打会被跳过并写入日志。

The servo holds the strike command for 300 ms before returning to rest. If another nod occurs while the servo is busy, the nod is still counted, but the strike is skipped and logged.

“舵机忙碌”依据软件计时判断，系统没有读取舵机实际位置。

The busy state is determined by software timing. The system does not read the servo’s actual position.

## 5. 屏幕布局｜Screen Layout

屏幕采用 320×240 横屏坐标，主要分为四个区域：

The screen uses a 320×240 landscape layout with four main areas:

- 顶部：最近四条运行日志，最新记录显示在最上方。
- 左侧：扇形姿态显示，包括零位边界、采样边界和实时指针。
- 右侧：点头次数、参考幅度、当前偏离角度、击打次数和执行机构状态。
- 底部：RESET 与 SAMPLE 触摸按钮。

- Top: the four most recent log entries, with the newest first.
- Left: a fan-shaped posture display with zero and sampled boundaries and a live pointer.
- Right: nod count, reference amplitude, current deviation, strike count, and actuator status.
- Bottom: RESET and SAMPLE touch controls.

浏览器模拟器沿用主要布局，但其绘图、附加状态和演示行为与固件并非完全一致。

The browser simulator follows the main layout, but its rendering, additional status indicators, and demonstration behavior are not identical to the firmware.

## 6. 项目目录｜Project Structure

    AccessibleInstruments/
    ├── ino_code/
    │   └── nodkick.ino
    ├── STEP最终版汇总/

部分目录保留了早期版本资料。当前固件使用现场 RESET / SAMPLE 标定，不应将历史参数生成流程视为当前固件的必需步骤。

Some directories retain earlier development materials. The current firmware uses live RESET / SAMPLE calibration; historical parameter-generation workflows should not be treated as required steps for this firmware.

## 7. 启动浏览器模拟器｜Run the Browser Simulator

模拟器使用 HTML、CSS 和 JavaScript，无需安装前端依赖。

The simulator uses HTML, CSS, and JavaScript and requires no frontend dependencies.

在项目根目录运行：

Run this command from the project root:

    python -m http.server 4173 --directory ./c_code/frontend

然后在浏览器访问：

Then open:

    http://127.0.0.1:4173/

也可以直接使用浏览器打开 `c_code/frontend/index.html`。

Alternatively, open `c_code/frontend/index.html` directly in a browser.

### 演示操作｜Demo Procedure

1. 点击屏幕中的 RESET。
2. 将右侧姿态偏移滑块调整到大于 2° 的值，例如 12°。
3. 点击屏幕中的 SAMPLE。
4. 点击“模拟一次点头”。
5. 查看 NODS、KICKS、指针及日志变化。

1. Click RESET on the simulated screen.
2. Move the posture slider above 2°, for example to 12°.
3. Click SAMPLE.
4. Click “模拟一次点头” to simulate one nod.
5. Observe the NODS and KICKS counters, pointer, and log.

右侧连接开关只改变模拟连接状态；清空日志不会清除当前标定和计数。

The connection toggle changes only the simulated connection state. Clearing the log does not reset calibration or counters.

## 8. 固件配置｜Firmware Configuration

当前源码声明的目标开发板及依赖为：

The current source declares the following target board and dependencies:

- Board：`esp32:esp32:m5stack_core2`
- M5Unified：`0.2.26`
- NimBLE-Arduino：`2.5.1`

主要硬件参数如下：

The main hardware settings are:

- 传感器地址 / Sensor address：`fd:3a:82:2b:45:bd`
- PCA9685 I²C 地址 / PCA9685 I²C address：`0x40`
- SDA：`32`
- SCL：`33`
- PWM 通道 / PWM channel：`15`
- PWM 频率 / PWM frequency：`50 Hz`
- 击打脉宽 / Strike pulse：`700 μs`
- 静止脉宽 / Rest pulse：`1000 μs`
- 击打保持时间 / Strike hold time：`300 ms`

这些数值对应当前源码和机构配置。更换传感器或改变舵机安装位置后，应重新核对地址、运动方向和机械行程。

These values correspond to the current source and mechanism configuration. After replacing the sensor or changing the servo installation, recheck the address, motion direction, and mechanical travel.

在 Arduino IDE 中使用时，可将源码放入同名草图目录 `nodkick/nodkick.ino`，然后选择对应开发板和端口进行编译、上传。

For use in the Arduino IDE, place the source in a matching sketch directory, `nodkick/nodkick.ino`, then select the appropriate board and port to compile and upload.

## 9. STEP 模型说明｜STEP Model Notes

汇总文件夹包含耳挂主体、定位插头、舵机支架单件及总成、盒体、盒盖和接口法兰等文件。

The consolidated folder includes the ear-hook frame, locator plug, individual servo-bracket parts and assembly, enclosure body, lid, and interface flange.

### 版本选择｜Selected Versions

- 耳挂主体采用 V4。
- 独立 Type-C 插头采用前端延长 2 mm 的版本。
- 舵机固定架采用重新建模的主支架、上压片和下压片。
- 盒体与盒盖采用 `display_enclosure_110` 中的版本。

- The ear-hook frame uses revision V4.
- The separate Type-C locator uses the version extended by 2 mm.
- The servo mount uses the rebuilt main bracket, upper clamp, and lower clamp.
- The enclosure body and lid use the versions in `display_enclosure_110`.

### 总成与单件区别｜Assemblies and Individual Parts

右耳挂总成 STEP 包含厂家模块参考体及原定位头，因此不能把该总成视为已替换加长插头的最新完整装配。

The ear-hook assembly STEP includes the manufacturer’s reference module and the original locator. It should not be treated as an updated assembly containing the extended plug.

舵机固定架总成包含三个支架零件，不包含实际舵机或金属立杆实体。

The servo-bracket assembly contains three bracket parts. It does not include the physical servo or metal upright.

### 接口法兰命名｜Interface Flange Naming

汇总中标为“六角杆接口”的文件来源于 `flange_D20_H10_bore6p2.step`。现有模型的中心孔为 Ø6.2 mm 圆孔，不是六角孔；文件名不能作为六角配合已经验证的依据。

The file labeled “六角杆接口” comes from `flange_D20_H10_bore6p2.step`. Its center bore is circular, with a diameter of 6.2 mm, rather than hexagonal. The filename does not establish verified compatibility with a hexagonal shaft.

## 10. 验证与限制｜Validation and Limitations

浏览器模拟器已经验证基本的 RESET、SAMPLE 和点头演示交互。机械模型目录保留了几何与导出检查记录，具体检查范围应以各模型报告为准。

The browser simulator has been checked for basic RESET, SAMPLE, and nod-demonstration interactions. Mechanical-model directories contain geometry and export checks; consult the individual reports for their scope.

当前点头识别使用加速度向量夹角。快速运动带来的动态加速度可能影响角度估算，因此不应将其视为不受运动影响的精确姿态测量。

Current nod detection uses the angle between acceleration vectors. Dynamic acceleration during rapid movement may affect the estimate, so it should not be treated as an exact orientation measurement under all conditions.

加长 Type-C 插头与简化厂家参考模型存在已记录的干涉。参考模型缺少真实插座内部结构，实际插入深度仍需实物确认。

The extended Type-C locator has documented interference with the simplified manufacturer reference model. That model omits the actual socket internals, so usable insertion depth still requires physical verification.

耳挂舒适度、插头配合、支架承载、舵机行程、击打效果及长期可靠性仍需要实物测试。

Ear-hook comfort, plug fit, bracket strength, servo travel, strike performance, and long-term reliability still require physical testing.

## 11. 后续开发方向｜Future Development

- 统一浏览器模拟器与固件的状态机和显示行为。
- 增加传感器断连、数据过期和执行机构异常的测试。
- 优化不同使用者的标定流程与交互提示。
- 根据实际佩戴和演奏反馈调整机械结构。
- 验证连续演奏时的响应、重复性和耐久性。

- Align the simulator’s state machine and display behavior with the firmware.
- Expand tests for sensor disconnection, stale data, and actuator faults.
- Improve calibration and interaction guidance for different users.
- Refine mechanical parts using real fitting and performance feedback.
- Evaluate response, repeatability, and durability during sustained use.
