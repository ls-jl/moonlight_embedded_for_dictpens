# RK3562 Moonlight Embedded 词典笔 Miniapp

本项目把 Rockchip RK3562 版本的 Moonlight Embedded 打包进有道/Falcon miniapp。miniapp 提供主机配对、主机选择和串流参数设置页面；开始串流后进入空白页面，由内置的 Moonlight 二进制直接接管 DRM framebuffer 输出。

## 包含内容

- Falcon miniapp 设置界面：主机配对、主机选择、串流参数配置。
- 内置 RK3562 Moonlight runtime：`assets/moonlight-rk3562`。
- miniapp 原生 JSAPI 模块：`jsapi/src/jsapi_rgbframe`。
- Moonlight RK DRM/MPP/RGA 后端适配：`moonlight-embedded-master`。
- PVE 交叉编译产物快照：`build-rk3562-from-pve`。

## 运行流程

1. `src/pages/index/index.vue` 显示竖屏设置页。
2. 主机配对数据存储在 miniapp 数据目录中，不硬编码。
3. 点击开始串流后跳转到 `src/pages/frame/frame.vue`。
4. frame 页面把内置 Moonlight runtime 拷贝到可写数据目录，并启动 Moonlight 子进程。
5. Moonlight 使用 RK DRM 后端抢占 framebuffer plane。
6. 退出 frame 页面时停止 Moonlight，并释放 framebuffer。
7. 如果 Moonlight 异常退出，frame 页面 watchdog 会自动回到设置页。

## 当前默认串流参数

- 应用：`Desktop`
- 编码：H.264
- 默认主机输入：`192.168.100.120`
- 默认码率：`1500 Kbps`
- 码率滑块范围：`500 Kbps` 到 `25000 Kbps`
- 包大小：`1024`
- Remote 模式：`yes`
- 帧率：`30` 或 `60`
- 方向：
  - 竖向
  - 竖向翻转
  - 横向
  - 横向翻转
- 输入模式：
  - 仅观看
  - 直接触摸屏
  - 触摸板相对鼠标
  - USB OTG 手柄

## RK 适配点

RK 视频后端位于 `moonlight-embedded-master/src/video/rk.c`。

已实现：

- MPP 硬解码。
- DRM plane 显示。
- 优先使用 DRM plane rotation。
- DRM plane 不支持旋转时，使用 RGA 硬件旋转 `90`、`180`、`270` 度。
- 不做 CPU 旋转 fallback。
- `90` 和 `270` 度旋转时，内部交换请求串流尺寸，旋转后仍输出到目标屏幕尺寸。

输入相关修改主要在 `moonlight-embedded-master/src/input/evdev.c`：

- 触摸屏模式。
- 触摸板模式，模拟相对鼠标移动。
- 手柄热插拔 rescan。
- DualShock 4 fallback 映射。
- Sony 手柄上报为 Xbox controller，提升主机兼容性。
- 只在手柄状态变化时发送 controller event，减少控制流压力。

miniapp 原生桥接位于 `jsapi/src/jsapi_rgbframe/JSRgbFramePlayer.cpp`：

- 启动和停止 Moonlight 进程。
- 将内置 runtime 文件复制到可写 app 数据目录。
- 串流期间保持亮屏。
- 输入模式下切换 RK USB OTG 到 host 模式。
- 串流期间持续重设 Wi-Fi 和 USB runtime power。
- 将 miniapp JS 侧串流参数转换为 Moonlight CLI 参数。

## 目录结构

```text
.
├── assets/moonlight-rk3562/        # 打包进 miniapp 的 Moonlight runtime
├── build-rk3562-from-pve/          # 从 PVE 拉回的 RK3562 编译产物
├── jsapi/                          # 原生 rgbframe JSAPI 模块
├── libs/                           # miniapp 原生库
├── moonlight-embedded-master/      # 修改后的 Moonlight Embedded 源码
├── src/pages/index/                # 设置和配对页面
├── src/pages/frame/                # 空白串流生命周期页面
├── tools/                          # miniapp 文档和工具链归档
├── package.json                    # Falcon miniapp 构建脚本
└── icon.png                        # miniapp 图标
```

## 构建 Miniapp 包

```bash
npm run build:bin
```

生成文件：

```text
8001780158276510.1_0_0.amr
```

`.amr` 是生成产物，已被 Git 忽略。

## 交叉编译原生产物

当前流程使用 PVE 编译机和 RK3562 工具链。凭据不要写入仓库，应通过本地 shell、SSH config 或环境变量提供。

在 PVE 上编译 Moonlight：

```bash
cmake --build /home/pve/moonlight_embedded_rk3562_20260531_120550/moonlight-embedded-master/build-rk3562 --parallel
```

在 PVE 上编译 JSAPI：

```bash
cmake --build /home/pve/moonlight_embedded_rk3562_20260531_120550/jsapi/build-rk3562 --parallel
```

拉回产物后，更新：

```text
assets/moonlight-rk3562/moonlight
build-rk3562-from-pve/moonlight-rk3562-runtime/moonlight
libs/arm64-orange/libjsapi_rgbframe.so
libs/libjsapi_rgbframe_12345.so
```

然后重新打包：

```bash
npm run build:bin
```

## 安装到设备

ADB 可用时：

```bash
adb push 8001780158276510.1_0_0.amr /userdisk/8001780158276510.1_0_0.amr
adb shell 'miniapp_cli install /userdisk/8001780158276510.1_0_0.amr'
adb shell 'miniapp_cli start 8001780158276510 --index'
```

使用设备 SSH 时，先把包复制到 `/userdisk/8001780158276510.1_0_0.amr`，再执行：

```bash
miniapp_cli install /userdisk/8001780158276510.1_0_0.amr
miniapp_cli start 8001780158276510 --index
```

## 设备运行路径

安装包路径是 A/B slot 形式，通常是：

```text
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/a
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/b
```

可写 app 数据目录：

```text
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/data
```

Moonlight runtime 和 key 会复制到：

```text
data/moonlight/runtime
data/moonlight/keys
```

常用日志：

```text
data/moonlight/moonlight-drm.log
/tmp/moonlight-pair.log
/tmp/moonlight_start.log
```

## 设备调试命令

查看 Moonlight 和 miniapp 进程：

```bash
ps | grep -E 'miniapp|moonlight' | grep -v grep
```

查看屏幕状态：

```bash
hal-screen state
```

手动保持亮屏：

```bash
hal-screen keep
```

查看 Wi-Fi 电源状态：

```bash
iwconfig wlan0
```

查看 USB OTG 模式：

```bash
cat /sys/devices/platform/ff740000.usb2-phy/otg_mode
```

必要时手动切换 USB 模式：

```bash
echo host > /sys/devices/platform/ff740000.usb2-phy/otg_mode
echo otg > /sys/devices/platform/ff740000.usb2-phy/otg_mode
```

查看 DRM 状态：

```bash
cat /sys/kernel/debug/dri/0/state
```

## 排查说明

- 如果串流页黑屏且没有恢复，先看 `moonlight-drm.log`。Moonlight 退出后 frame 页面应自动回设置页。
- 大量 `Network dropped ... frames` 通常说明 Wi-Fi 质量、MTU、码率或主机侧负载有问题。默认配置使用 `-remote yes -packetsize 1024` 降低分片风险。
- 如果 PS4 手柄断开，查看 USB host reset 和 Type-C 事件：

```bash
dmesg | tail -n 200
```

- 输入模式看不到手柄时，确认设备处于 USB host 模式，并检查 `/proc/bus/input/devices`。
- 安装包从 slot `a` 切到 slot `b` 后，调试路径也要同步改成当前 slot。

## 生成文件和本地文件

以下内容是构建或本地测试输出，不作为源码真相来源：

- `.falcon_`
- `.falcon_tmp`
- `8001780158276510.1_0_0.amr`
- 设备截图
- 本地日志

原生 runtime 的源码真相来自当前源码树，以及复制到 `assets/`、`libs/`、`build-rk3562-from-pve/` 的当前编译产物。
