# frontcamscan 扫词 JSAPI Demo
## 当前能力

- 通过 `global` 模块注册当前 miniapp 为 OCR 使用方。
- 通过 `global.startScan()` 触发扫词。
- 支持普通扫词和多行扫描两种入口。
- 接收 OCR 过程事件、OCR 结果、查词结果和扫描异常事件。
- 通过 `littleP` 模块打开小P原图同步链路。
- 在小P `update` / `end` 扫描状态下接收原始图片数据。
- 兼容小P返回参数顺序不稳定的情况，自动识别哪个参数包含图片。

## 结构示例

```text
frontcamscan/
├── 8001779179340550.1_0_0.amr        # 最近一次构建出的 miniapp 安装包
├── README.md                         # 本说明
├── icon.png                          # miniapp 图标
├── package.json                      # appid、版本、构建脚本、依赖
├── libs/
│   └── arm64-orange/
│       ├── libbusiness_littlep_702767252.so
│       └── libjsapi_littlep_702767252.so
├── src/
│   ├── app.js                        # miniapp 入口，注册 BasePage
│   ├── app.json                      # 页面路由配置
│   ├── base-page.js                  # 页面生命周期和事件释放基类
│   ├── pages/
│   │   └── index/
│   │       └── index.vue             # 扫词 demo 主页面
│   └── utils/
│       ├── global.js                 # global JSAPI 单例封装
│       └── littlep.js                # littleP JSAPI 实例和信号绑定封装
└── tools/
    ├── miniapp_docs.md               # miniapp 开发文档
    ├── KEYBOARD_INPUT.md             # 键盘输入相关说明
    └── *.tar.bz2                     # 交叉编译工具链归档
```

## 运行环境

- 设备：支持 miniapp 框架和 `global` / `littleP` native 模块的词典笔 Linux 设备。
- 架构：当前携带的 native 库是 AArch64 ELF，放在 `libs/arm64-orange/`。
- 本机工具：`aiot-cli`、`adb`，以及项目所需的 Node/npm 环境。
- appid：`8001779179340550`，定义在 `package.json`。

## 构建和安装

构建：

```sh
npm run build
```

构建后会生成类似下面的安装包：

```text
8001779179340550.1_0_0.amr
```

通过 adb 安装并启动：

```sh
PATH=/Users/me/platform-tools:$PATH aiot-cli adb 8001779179340550.1_0_0.amr
```

也可以在设备上手动启动：

```sh
PATH=/Users/me/platform-tools:$PATH adb shell miniapp_cli start 8001779179340550 index
```

## 扫词调用链

页面入口在 `src/pages/index/index.vue`。

普通扫词按钮调用 `startWordScan()`：

```js
this.enableScan(false, 0)
this.callGlobal('startAlmondMultiScan', [0], true)
this.callGlobal('startScan', [], false)
```

多行扫描按钮调用 `startMultiLineScan()`：

```js
this.enableScan(false, 1)
this.callGlobal('startAlmondMultiScan', [1], true)
this.callGlobal('startScan', [], false)
```

其中 `enableScan(showLog, scanMethod)` 会先完成三件事：

1. 初始化并绑定 `global` 事件。
2. 初始化并绑定 `littleP` 事件。
3. 同时打开系统 OCR 扫描链路和小P原图链路。

核心顺序如下：

```js
this.setCurrentApp()
this.ensureLittlePReady()
this.setLittlePScanEnabled(true)
this.callGlobal('setShowImgScanMode', [true], true)
this.callGlobal('setIsOpenScan', [true], true)
this.callGlobal('addUseOcrAppId', [this.buildOcrAppConfig(scanMethod)], false)
```

关闭扫词时调用 `disableScan()`，会反向关闭小P原图开关、系统扫词开关，并注销当前 appid：

```js
this.setLittlePScanEnabled(false)
this.callGlobal('setShowImgScanMode', [false], true)
this.callGlobal('setIsOpenScan', [false], true)
this.callGlobal('deleteUseOcrAppId', [this.appid], true)
```

## `global` 模块说明

`global` 是设备 miniapp 框架提供的系统级 JSAPI。项目在 `src/utils/global.js` 中做了单例封装：

```js
import globalModule from 'global'

let globalManager = null

export function getGlobalModule() {
  if (!globalManager) {
    globalManager = new globalModule.Global()
  }
  return globalManager
}
```

### 当前使用的方法

| 方法 | 调用参数 | 作用 |
| --- | --- | --- |
| `setCurrentAppId` | `(appid, true)` | 告诉系统当前前台 miniapp 的 appid。页面显示或启用扫词前会调用。 |
| `setShowImgScanMode` | `(true / false)` | 打开或关闭扫词原图模式。只打开这个还不够，原图链路还需要小P `setScanEnabled`。 |
| `setIsOpenScan` | `(true / false)` | 打开或关闭系统扫描能力。 |
| `addUseOcrAppId` | `(JSON.stringify(config))` | 注册当前 app 为 OCR 结果接收方。失败时 demo 会认为启用扫词失败。 |
| `deleteUseOcrAppId` | `(appid)` | 注销当前 app 的 OCR 使用权。 |
| `startAlmondMultiScan` | `(0 / 1)` | 设置扫描模式。当前约定 `0` 为普通扫词，`1` 为多行扫描。 |
| `startScan` | `()` | 触发实际扫描。 |

`addUseOcrAppId` 使用的配置格式：

```js
JSON.stringify({
  appid: this.appid,
  scanMethod,
  stopSoundPlayerOnHeadButtonPressed: false,
})
```

`scanMethod` 当前使用：

- `0`：普通扫词。
- `1`：多行扫描。

### 当前监听的 `global` 信号

| 信号 | 回调参数 | 用途 |
| --- | --- | --- |
| `ocrStartChanged` | `(isNewScan)` | OCR 开始。 |
| `ocrStopChanged` | `(ocrResult, scanType)` | OCR 停止，通常包含一次 OCR 结果。 |
| `ocrCompleteChanged` | `(ocrResult, scanType)` | OCR 完成。 |
| `ocrResultChanged` | `(ocrResult)` | OCR 中间或最终文本结果。 |
| `scanWordResultChanged` | `(dictType, result)` | 查词结果。 |
| `apolloScanWordResultChanged` | `(dictType, result)` | Apollo 查词结果。 |
| `scanErrorChanged` | `(result, code)` | 扫描异常。 |
| `stopContinueScanChanged` | `()` | 连续扫描停止。 |
| `onIsMultiScanMode` | `(mode)` | 多行扫描模式状态。 |
| `onShowOcrImgResult` | `(result)` | 可选原图结果事件，设备上不一定存在。 |
| `showOcrImgResult` | `(result)` | 可选原图结果事件，设备上不一定存在。 |
| `showOcrImgResultChanged` | `(result)` | 可选原图结果事件，设备上不一定存在。 |

当前测试中，OCR 文本结果主要来自 `global` 信号；原始图片主要来自 `littleP` 信号。

## `littleP` 模块说明

`littleP` 是从小P应用中提取并随包携带的 native JSAPI。项目在 `src/utils/littlep.js` 中做了兼容封装：

```js
import LittleP from 'littleP'
```

不同框架版本可能把 native 模块暴露成构造函数、单例对象、`default` 对象或 `LittleP.LittleP` 构造函数，所以 `createLittlePManager()` 会按顺序探测可用形态，最终返回带有 `requestData()` 的 manager。

信号绑定也做了两套兼容：

- `signal.connect(handler)` / `signal.disconnect(token)`
- `signal.on(handler)` / `signal.off(token)`

### 当前使用的方法

原图链路只主动调用一个接口：

```js
manager.requestData(
  'local/interface/setScanEnabled',
  { scanEnabled: 'enabled' },
  []
)
```

关闭时调用：

```js
manager.requestData(
  'local/interface/setScanEnabled',
  { scanEnabled: 'disabled' },
  []
)
```

注意：

- 参数 key 必须是 `scanEnabled`。
- 开启值必须是字符串 `enabled`。
- 关闭值当前使用字符串 `disabled`。
- 如果没有先开启这个开关，小P native 侧的 `syncGetOcrImage()` 会直接返回，不会把原图数据发给 JS。

### 当前监听的 `littleP` 信号

| 信号 | 回调参数 | 用途 |
| --- | --- | --- |
| `onRequestDataResult` | `(type, firstValue, secondValue)` | 接收 `requestData` 或 native 自动同步回来的结果。原图数据在这里返回。 |
| `onScanStatusResult` | `(status)` | 接收小P扫描状态，例如 `update`、`end`。 |
| `onUploadSignal` | `(value)` | 可选上传相关事件，仅记录日志。 |

`onRequestDataResult` 的参数顺序在当前设备上不能强假设。小P原图回调里，一个参数可能是任务名或 task id，另一个参数才是 JSON 图片数据。因此 demo 使用 `littlePResultParts(firstValue, secondValue)` 自动判断哪个参数包含图片。

## native `.so` 库说明

两个库都放在 `libs/arm64-orange/`，构建时会随 miniapp 包安装到设备。不要随意改名或移动目录，native 模块加载依赖 miniapp 框架的 ABI 目录约定。

### `libjsapi_littlep_702767252.so`

类型：

```text
ELF 64-bit LSB shared object, ARM aarch64, dynamically linked, stripped
```

作用：

- 把小P native 能力桥接成 JS 里的 `littleP` 模块。
- 暴露 `requestData()` 调用入口。
- 暴露 `onRequestDataResult`、`onScanStatusResult` 等 JS 信号。
- 监听 native 扫描状态。当状态字符串包含 `"status": "update"` 或 `"status": "end"` 且不是失败状态时，会触发业务库同步取图逻辑。

已从库字符串确认的关键符号或字符串：

- `onRequestDataResult`
- `onScanStatusResult`
- `"status": "update"`
- `"status": "end"`
- `YLittlePManager::syncGetOcrImage(bool)`

### `libbusiness_littlep_702767252.so`

类型：

```text
ELF 64-bit LSB shared object, ARM aarch64, dynamically linked, stripped
```

作用：

- 实现小P业务层 `YLittlePManager`。
- 实现 `requestData()` 分发的本地接口。
- 维护全局开关 `YLITTLEP::scanEnabled`。
- 在扫描 `update` / `end` 阶段同步获取 OCR 原图并编码。

已从库字符串确认的关键接口和符号：

- `local/interface/setScanEnabled`
- `scanEnabled`
- `getOcrImage`
- `getOcrEndImage`
- `getBase64StringDataFromFile`
- `syncGetOcrImage`
- `YLittlePManager::syncGetOcrImage is called, isEndImage:`

### 关于 `getOcrImage` / `getOcrEndImage`

业务库里确实存在这些接口，但当前 demo 不主动调用它们：

- `local/interface/getOcrImage`
- `local/interface/getOcrEndImage`
- `local/interface/getBase64StringDataFromFile`

原因是设备实测中，手动直接调用 `getOcrImage` / `getOcrEndImage` 容易导致 miniapp 框架崩溃。当前稳定方式是：

1. 先调用 `local/interface/setScanEnabled` 开启小P扫描原图链路。
2. 让扫描状态 `update` / `end` 驱动 native 自动调用 `syncGetOcrImage(false / true)`。
3. 从 `onRequestDataResult` 接收结果。

## 原始图片返回格式

当前小P链路返回的“原始图片”不是文件路径，而是 JSON 内的 JPEG base64 字符串。

典型数据形态如下：

```json
{
  "height": 113,
  "path": "/9j/..."
}
```

这里的 `path` 字段名容易误导。它不是 `/userdisk/camera/xxx.jpg`，而是 JPEG base64 内容。demo 会识别 `/9j/` 开头的 JPEG base64，并转换成：

```text
data:image/jpeg;base64,/9j/...
```

再传给 `<image :src="rawImageSrc" />` 预览。

当前解析逻辑支持：

- 绝对图片路径：`/xxx/yyy.jpg`
- `file://` 图片路径
- `data:image/...;base64,...`
- 裸 JPEG/PNG/BMP base64
- 对象字段：`path`、`base64`、`imageBase64`、`imgData`、`dataUrl`、`rawImagePath` 等
- 数组或嵌套对象中的图片字段

## 页面行为

页面分为几块：

- 状态：显示扫词开关、是否正在扫描、扫描类型。
- 操作：`启用并开始` / `开始扫词`、`多行扫描`、`关闭扫词`。
- OCR：展示最近一次 OCR 文本。
- 查词结果：展示最近一次查词结果。
- 原始图片：展示小P返回的原图预览和解析出的路径或数据类型。
- 事件：展示最近 10 条调试事件，同时也会写入设备日志。

## 日志和排障

设备上 `adb logcat` 不一定可用。当前设备可以直接看应用日志：

```sh
PATH=/Users/me/platform-tools:$PATH adb shell "tail -n 400 /userdata/applog/YD_PEN_APP.log | grep -i -E 'frontcamscan|littlep|ylittlep|syncgetocrimage|setscanenabled|requestdata|crash'"
```

确认 miniapp 是否正在运行：

```sh
PATH=/Users/me/platform-tools:$PATH adb shell miniapp_cli memoryApp
```

确认包内 JS 是否已更新到设备：

```sh
PATH=/Users/me/platform-tools:$PATH adb shell "grep -a -n littlePResultParts /userdisk/secondary/miniapp/data/mini_app/pkg/8001779179340550/*/app.js 2>/dev/null"
```

如果 OCR 正常但原图不返回，优先检查：

1. 页面是否先调用了 `littleP scan mode: enabled`。
2. 日志里是否出现 `YLittlePManager::syncGetOcrImage is called`。
3. `onRequestDataResult` 是否返回了含 `height,path` 的 JSON。
4. `path` 是否是 `/9j/` 开头的 base64，而不是文件路径。
5. 是否误用了手动 `getOcrImage` / `getOcrEndImage` 接口。

如果出现框架崩溃，优先排查：

- 是否重新引入了手动 `local/interface/getOcrImage` 调用。
- 是否在未完成 `setScanEnabled` 前强行取图。
- 是否在页面销毁时没有关闭扫词链路。
- 是否把大型 base64 内容完整写入 UI 或日志，导致渲染或日志压力过大。

## 关键实现文件

### `src/pages/index/index.vue`

主页面，包含：

- UI 展示。
- 扫词启停。
- `global` 事件绑定。
- `littleP` 事件绑定。
- OCR 文本解析。
- 原图 base64 / 路径解析。
- 最近事件日志。

### `src/utils/global.js`

只负责创建并复用 `globalModule.Global()`。

### `src/utils/littlep.js`

负责兼容不同 native 模块导出形态，并提供统一的 signal 绑定释放包装。

### `src/base-page.js`

封装页面生命周期。页面显示时会转发到 Vue 根组件的 `onShow()`，页面卸载时会释放通过 `$falcon.on()`、`setTimeout()`、`setInterval()` 注册的资源。

## 注意事项

- 本项目依赖设备私有 JSAPI，不能在普通 Web 浏览器里运行。
- `littleP` 两个 `.so` 是设备和架构相关的 native 库，仅适用于当前 AArch64 目标环境。
- 原图链路返回的是 base64 图片数据，不应继续按 `/userdisk/camera` 文件落盘路径排查。
- `setShowImgScanMode(true)` 和 `setScanEnabled(enabled)` 是两条不同链路，两个都需要。
- 关闭页面或退出前要调用关闭逻辑，避免扫词状态遗留给其他 miniapp。
