# 小程序开发文档 - 完整技术规范

> 本文档包含所有小程序开发相关的技术规范，按目录结构组织，支持展开/折叠查看。

## 目录结构

以下文档按原始目录结构组织，点击目录可展开/折叠查看详细内容：

<details>
  <summary><strong>📁 JSAPI</strong></summary>
  <div style="margin-left: 20px;">
    <details>
      <summary><strong>📁 JSAPI扩展方案V2</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 JSAPI - hello模块封装讲解</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # JSAPI - hello模块封装讲解

## 模块功能

hello是 JSAPI的开发案例，向用户演示最常用的三种调用模式，以及常用的入参出参C++用法：

1. **同步调用**（JS线程同步调用C++方法）
2. **异步调用**（调度到非JS线程调度C++方法）
3. **消息publish**（在任意时间和位置通过C++向JS发送相关消息数据）

## 同步调用

函数原型为 `void functionName(JQFunctionInfo &info)`

例子中写法：

```cpp
// 简单同步接口
void joinPath(JQFunctionInfo &info);
```

**注册方式：**

```cpp
tpl->SetProtoMethod("joinPath", &JSFoo::joinPath);
```

**线程调度：**

`JSFoo::joinPath` 被调度时在JS线程，因此不可以做阻塞操作，例如磁盘、网络IO等，不然会阻塞JS和卡UI

**入参和出参：**

*   **入参**：通过 `info[0]` 获取 `JSValue` 格式的参数，详细可以看《JSAPI - 入参和解析》
*   **出参**：通过 `info.GetReturnValue().Set(result)` 形式，可以返回原子类型，`int` `bool` `string` 等等，或者返回一个 `JSValue`

```cpp
void JSFoo::joinPath(JQFunctionInfo &info) {
    JSContext *ctx = info.GetContext();
    std::vector<std::string> slices;
    for (unsigned idx = 0; idx < info.Length(); idx++) {
        slices.push_back(JQString(ctx, info[idx]).getString());
    }
    info.GetReturnValue().Set(JQuick::pathjoin(slices));
}
```

**JS使用方法：**

```cpp
import { foo } from 'hello'
foo.joinPath('root', 'works', 'project')
console.log(`pathJoin result ${path}`)
```

## 异步调用

函数原型为 `void functionName(JQAsyncInfo &info)`

例子中写法：

```cpp
// 文件异步接口
void readFile(JQAsyncInfo &info);
```

**注册方式：**

```cpp
tpl->SetProtoMethodPromise("readFile", &JSFoo::readFile);
```

**线程调度：**

`JSFoo::readFile` 被调度时在非JS线程，因此可以做一些阻塞操作，例如读写文件，不会影响JS线程和UI相关

**入参和出参：**

*   **入参**：通过 `info[0]` 获取 `Bson` 格式的参数，详细可以看《JSAPI - 入参和解析》
*   **出参**：
    1.  正常返回值用 `info.post(Bson result)` 形式
    2.  错误返回时用 `info.postError("some error %d", errorCode)` 形式

```cpp
void JSFoo::readFile(JQAsyncInfo &info) {
    std::string path = info[0].string_value();
    LOGD("JSFoo::readFile path: %s", path.c_str());
    // read file from disk
    std::string content = "abcd1234";
    info.post(content);
}
```

**JS使用方法：**

```cpp
import { foo } from 'hello'
async testReadFile() {
    const content = await foo.readFile('/some/path/of/file')
    console.log(`readFile result ${content}`)
}
```

## 消息publish

例子中写法：

1.  继承于 `JQPublishObject`

```cpp
class JSFooWifi : public JQPublishObject {
    ...
};
```

**注册方式：**

注意在最后需要写一句 `JSFooWifi::InitTpl(tpl)` 给类型初始化 `on` `off` 方法

```cpp
JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "fooWifi");
// 设定 C++ 对象工厂函数
tpl->InstanceTemplate()->setObjectCreator([]() {
    return new JSFooWifi();
});
tpl->SetProtoMethodPromise("scanWifi", &JSFooWifi::scanWifi);
JSFooWifi::InitTpl(tpl);
```

**线程调度：**

在某些方法中（可以是JS线程或非JS线程），直接调用 `JQPublishObject::publish` 函数，会将该数据发送到JS空间，调用`on`方法注册的某些topic的回调中。

**入参出参：**

出参原型：`void publis(string topic, Bson bson)`

这里我们以一种类 JSON 的格式 `Bson` 来传递C++数据，该数据会自动转换成 JS 变量，并抛送到 JS 对应的监听函数中去，用法示例：

```cpp
void scanWifi(JQAsyncInfo &info) {
    // 模拟通知 JS 空间扫描结果
    Bson::array result;
    result.push_back("ssid0");
    result.push_back("ssid1");
    result.push_back("ssid2");
    publish("scan_result", result);
    // 异步接口必须回调
    info.post(0);
}
```

**JS使用方法：**

注意`fooWifi.off` 可以立即解除数据监听，入参为 `fooWifi.on` 函数的返回值（token）

原型分别为：
*   `int on(string topic, function callback) -> 返回 token`
*   `void off(int token)`

```cpp
import { fooWifi } from 'hello'
testScanWifi() {
    fooWifi.on('scan_result', (res) => {
        console.log(`got scan_result ${JSON.stringify(res)}`)
    })
    fooWifi.scanWifi()
}
```

## 其他

### JS模块定义

#### 单例方式

目前推荐以类的形式导出，C++如下：

```cpp
JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "foo");
// 设定 C++ 对象工厂函数
tpl->InstanceTemplate()->setObjectCreator([]() {
    return new JSFoo();
});
tpl->SetProtoMethod("joinPath", &JSFoo::joinPath);
tpl->SetProtoMethodPromise("readFile", &JSFoo::readFile);
tpl->SetProtoMethodPromise("requestHttp", &JSFoo::requestHttp);
// 导出该类的一个实例
env->setModuleExport("foo", tpl->CallConstructor());
```

**JS中调用方式**

1.  导出该类的一个实例用法：

```cpp
import { foo } from 'hello'
foo.joinPath('some', 'path')
```

#### new 对象方式

参考C++

```cpp
JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "foo");
// 设定 C++ 对象工厂函数
tpl->InstanceTemplate()->setObjectCreator([]() {
    return new JSFoo();
});
tpl->SetProtoMethod("joinPath", &JSFoo::joinPath);
tpl->SetProtoMethodPromise("readFile", &JSFoo::readFile);
tpl->SetProtoMethodPromise("requestHttp", &JSFoo::requestHttp);
// 导出该类原型
env->setModuleExport("foo", tpl->GetFunction());
```

**JS中调用方式**

```javascript
import { foo } from 'hello'
var tt = new foo();
tt.on('scan_result', (res) => {
    console.log(`got scan_result ${JSON.stringify(res)}`)
})
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 JSAPI - 入参和解析</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # JSAPI - 入参和解析

## JS 入参方式

接口入参方式分两种：

*   **参数列表**：类似C函数，列表参数，例如 `testMethod(arg0, arg1, arg2,...)`；
    *   偏简单的接口传递方案，参数固定，但如果后期参数要修改，则需要改动代码
*   **字典参数（有名参数）**：例如 `testMethod({arg0: "val0", arg1: "val1", ...})`；
    *   options方案且方便扩展，类似C/C++结构体

字典参数可参考 Node.js 的参数定义文档：
例如 `fs.readFile`：
https://nodejs.org/api/fs.html#fspromisesreadfilepath-options
接口定义为：`fsPromises.readFile(path[, options])`

对于字典参数，JS有一种简略的写法：字面量对象，例如
```javascript
let arg0 = "val0"
let arg1 = "val1"
testMethod({arg0, arg1})
```
上面等价于
```javascript
testMethod({"arg0": arg0, "arg1": arg1})
```

## C++解析方式

1.  `JQFunctionInfo` 一般用 `JQTypes.h` 中的 `JQString` `JQObject` 等等辅助方法从JS变量解析到C++类型。
2.  `JQAsyncInfo` 就非常简单，会事先将 JS 入参变量序列化成类似json对象的Bson类型（支持binary），操作方式与JSON一致

### 第一种，参数列表

#### 1. JQAsyncInfo 解析方式

```cpp
// 假如 js 写法
let arg0 = "some string";
let arg1 = 100;
let arg2 = true;
testMethodAsync(arg0, arg1, arg2);

// C++ 解析方法
void testMethodAsync(JQAsyncInfo& info) {
    // 因为是异步调用，因此参数已经序列化成与JS变量无关的Bson结构，通过 info[] 下标的方式获取
    std::string arg0 = info[0].string_value();
    int arg1 = info[1].int_value();
    bool arg2 = info[2].bool_value();
    // 更多参数类型获取方式可以查看 bson.h
}
```

#### 2. JQFunctionInfo 解析方式

```cpp
// 假如 js 写法
let arg0 = "some string";
let arg1 = 100;
let arg2 = true;
testMethod(arg0, arg1, arg2)

// C++ 解析，方法 1
void testMethod(JQFunctionInfo& info) {
    JSContext* ctx; // js 运行环境
    std::string arg0 = JQString(ctx, info[0]).getString(); // 从JS变量中解析出字符串
    int arg1 = JQNumber(ctx, info[1]).getInt32(); // 从JS变量中解析出int32
    bool arg2 = JQBool(ctx, info[2]).getBool(); // 从JS变量中解析出 bool
    // 更多解析辅助方式，可以参考 JQTypes.h
}

// C++ 解析，方法2
// 可以看到async获取方式是非常简单的，因此同步方法这里也提供了对应形式，但是效率会略低点
void testMethod(JQFunctionInfo& info) {
    // 下面这句会将JS入参序列化成与JS无关的 Bson 结构，然后用法与async一致
    JQParamsHolder params = info.toParamsHolder();
    std::string arg0 = params[0].string_value();
    int arg1 = params[1].int_value();
    bool arg2 = params[2].bool_value();
}
```

### 第二种，字典参数

#### 1. JQAsyncInfo 解析方式

```cpp
// 假如 js 写法
let path = "/some/path"
let arg0 = "some string";
let arg1 = 100;
let arg2 = true;
testMethodAsync(path, {arg0, arg1, arg2});

// C++ 解析方式
void testMethodAsync(JQAsyncInfo& info) {
    // 因为是异步调用，因此参数已经序列化成与JS变量无关的Bson结构，通过 info[] 下标的方式获取
    std::string path = info[0].string_value();
    Bson::object opts = info[1].object_items();
    std::string arg0 = opts["arg0"].string_value();
    int arg1 = opts["arg1"].int_value();
    bool arg2 = opts["arg2"].bool_value();
    // 更多参数类型获取方式可以查看 bson.h
}
```

#### 2. JQFunctionInfo 解析方式

```cpp
// 假如 js 写法
let path = "/some/path"
let arg0 = "some string";
let arg1 = 100;
let arg2 = true;
testMethod(path, {arg0, arg1, arg2})

// C++ 解析，方法 1
void testMethod(JQFunctionInfo& info) {
    JSContext* ctx; // js 运行环境
    std::string path = JQString(ctx, info[0]).getString();
    JQObject opts(ctx, info[1]); // JS的第二个参数是一个object
    std::string arg0 = opts.getString("arg0");
    int arg1 = opts.getInt32("arg1");
    bool arg2 = opts.getBool("arg2");
    // 更多解析辅助方式，可以参考 JQTypes.h
}

// C++ 解析，方法2
// 可以看到async获取方式是非常简单的，因此同步方法这里也提供了对应形式，但是效率会略低点
void testMethod(JQFunctionInfo& info) {
    // 下面这句会将JS入参序列化成与JS无关的 Bson 结构，然后用法与async一致
    JQParamsHolder params = info.toParamsHolder();
    std::string path = params[0].string_value();
    Bson::object opts = params[1].object_items(); // JS 的第二个参数是object
    std::string arg0 = opts["arg0"].string_value();
    int arg1 = opts["arg1"].int_value();
    bool arg2 = opts["arg2"].bool_value();
    // 更多参数类型获取方式可以查看 bson.h
}
```

### 其他使用方法列举：

1.  **判断参数类型**
    a.  JQTypes 辅助方法：`JQObject(ctx, info[0]).isObject()`
    b.  Bson 方法：`params[0].is_object()`

其他复杂类型待列举，主要是 Object、Array，可以对应查看 `JQTypes.h` `bson.h`

## C++构造参数

### 第一种，普通构参

*   **map 构参**
    *   `Bson::object` 原型为 `std::map< Bson >`，其操作方法，下标操作等与 `std::map` 一致

```plaintext
Bson::object result;
result["key_int"] = 100;
result["key_str"] = "test_string";
result["key_double"] = 1.3;
result["key_bool"] = true;
result["key_null"] = Bson();
// 也可以用 insert find 等方法
// 注意：必要的时候要明确用强转或参数构造来明确类型，同 Bson::array
```

*   **array 构参**
    *   `Bson::array` 原型为 `std::vector< Bson >`，因此其操作方法，下标操作等与 `std::vector` 一致

```plaintext
Bson::array result;
result.push_back(100);  // int 类型
result.push_back("test_string");  // 字符串类型
result.push_back(1.3);  // double 类型
result.push_back(true);  // 布尔类型
result.push_back(Bson());  // null 类型
// 也可以 push_back Bson::object Bson::array 等嵌套结构
// 也可以用下标访问修改
result[0] = 101;
// 注意：必要的时候要明确用强转或参数构造来明确类型
result[2] = (double)1;
result[3] = Bson(true);
```

### 第二种，初始化构参

有时候构造参数为了减少代码量，会用初始化构造方式

*   **map 构造**

```plaintext
Bson::object result = {
    {"key_int", 100},
    {"key_str", "test_string"},
    {"key_double", 1.3},
    {"key_bool", true},
    {"key_null", Bson()}
};
```

*   **array 构造**

```plaintext
Bson::array result = {100, "test_string", 1.3, true, Bson()};
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 JSAPI-如何编译</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # JSAPI-如何编译

## Linux编译方法

### 下载代码

下载文件：`jqutil-example-v6.zip.pdf` (143 KB)

**注意**：下载后请将后缀 `.pdf` 去掉，保留 `.zip` 然后进行解压，并导入到你的开发IDE中。

代码目录结构如下：

```
.
├── jsapi
│   ├── build                // 编译目录
│   ├── iot-miniapp-sdk      // sdk 目录
│   │   ├── include          // sdk 的头文件
│   │   └── src              // sdk 的库文件
│   └── src
│       └── jsapi_hello      // jsapi hello 示例
└── ui                       // IoT 小程序示例，用于测试 jsapi hello 模块
```

### 配置toolchain

如果是交叉编译环境需要设置toolchain，否则可跳过该步骤。

打开 `jsapi/CMakeLists.txt` 文件：

```cmake
# 设置工具链前缀比如 /usr/bin/arm-linux-gnueabihf-
set(CROSS_TOOLCHAIN_PREFIX "arm-linux-gnueabihf-")
set(CMAKE_C_COMPILER "${CROSS_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${CROSS_TOOLCHAIN_PREFIX}g++")
```

### 编译

进入编译目录，执行cmake和make命令即可：

```bash
cd jsapi/build
cmake ..
make
```

编译输出 `.so` 文件：`jsapi/build/libjsapi_hello.so`

### 运行

1. 将 `libjsapi_hello.so` 拷贝到 `/etc/miniapp/jsapis`（Linux默认为etc路径）目录下：

```
/etc/miniapp/
└── jsapis
    └── libjsapi_hello.so  // 拷贝到这里
```

2. 用 IDE 打开 `ui` 目录，点击编译、推送按钮即可在设备上运行。

如下为运行的界面，点击相应的按钮测试：

- TESTJOINPATH
- TESTREADFILE
- TESTREQUESTHTTP
- TESTSCANWIFI
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 容器框架服务</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 容器全局信息</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 容器全局信息

## 环境变量 $falcon.env

**说明**：环境变量对象 API

**参数列表**：

| 参数 | 类型 | 描述 |
| :--- | :--- | :--- |
| platform | string | 运行平台：Darwin、Linux、Windows |
| version | string | UI框架版本，例如：1.5.0 |
| apiVersion | int | api版本，例如：版本1会有对应的通用功能api，以及jsapi接口 |
| deviceModel | string | 设备信息型号 |
| deviceWidth | int | 设备分辨率-宽度 |
| deviceHeight | int | 设备分辨率-高度 |

**示例**：
```javascript
let sysPlatform = $falcon.env.platform;
let sysVersion = $falcon.env.version;
let apiVersion = $falcon.env.apiVersion;
let deviceModel = $falcon.env.deviceModel;
let deviceWidth = $falcon.env.deviceWidth;
let deviceHeight = $falcon.env.deviceHeight;
```

## 其他

**参数列表**：

| 参数 | 类型 | 描述 |
| :--- | :--- | :--- |
| $workspace | string | 应用包安装路径；<br>提示：重新应用安装或者更新时，会删除该路径下的所有文件 |
| $dataDir | string | 应用自己的data路径；<br>提示：仅在应用删除的时候，删除该路径下所有文件 |
| $appid | string | 应用appid |

**示例**：
```javascript
let workspacePath = $falcon.$workspace;
let dataDir = $falcon.$dataDir;
let appid = $falcon.$appid;
```
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 框架内置JSAPI</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 File-文件管理(已不更新)</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # File-文件管理(已不更新)

## 说明

*   目前文件读写没有权限控制，给到非自己产品的客户开发应用时需谨慎
*   已不更新，新方案(需要框架V1.5版本)见 fs 文档

## 1. File JSAPI简介

File JSAPI是框架提供的一个轻量级文件接口，特别适用于存储小程序运行所需的文件，如文本、图片、视频等资源文件，这些文件都可以通过File JSAPI来存取。

File JSAPI的作用域为当前小程序应用，包含的接口如下：

| 接口宿主 | JSAPI | 调用方法 | 接口功能 |
| :--- | :--- | :--- | :--- |
| file | saveFile | `$falcon.jsapi.file.saveFile({apFilePath: 'xxx'})` | 文件管理 |
| | getSavedFileInfo | `$falcon.jsapi.file.getSavedFileInfo({apFilePath: 'xxx'})` | |
| | getFileInfo | `$falcon.jsapi.file.getFileInfo({apFilePath: 'xxx'})` | |
| | getSavedFileList | `$falcon.jsapi.file.getSavedFileList({})` | |
| | removeSavedFile | `$falcon.jsapi.file.removeSavedFile({apFilePath: 'xxx'})` | |

### 1.1 file.saveFile

`file.saveFile` 是保存文件到本地（本地文件大小总容量限制：10 MB）的 API，支持文件网络下载。

调用 my.saveFile 成功后，可在"MINIAPP_DATAROOT file/appid/"路径下查看保存的文件，其中appid是小程序的id。

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| apFilePath | String | 是 | 文件路径。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.saveFile({
    apFilePath: 'd:\\demo.jpg',
}, result => {
    console.log(result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| apFilePath | String | 文件保存路径。 |

### 1.2 file.getSavedFileInfo

`file.getSavedFileInfo` 是获取 "MINIAPP_DATAROOT file/appid/"路径下 保存的 所有 文件信息的 API，其中appid是小程序的id。

**入参**

入参为Object类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| apFilePath | String | 是 | 文件路径。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.saveFile({
    apFilePath: 'd:\\demo.jpg',
}, result => {
    result && !result.error && file.getSavedFileInfo({
        apFilePath: result["apFilePath"],
    }, result => {
        console.log(result);
    });
});
```

### 1.3 file.getFileInfo

`getFileInfo` 是获取文件信息的 API。

**入参**

入参为Object类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| apFilePath | String | 是 | 文件路径。（本地文件） |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.getFileInfo({
    apFilePath: "/data/mini_app/file/0000000000000001/demo.jpg",
}, result => {
    console.log(result);
});
```

**success 回调函数**

入参为Object类型，属性如下：

| 名称 | 类型 | 描述 |
| :--- | :--- | :--- |
| size | Number | 文件大小。 |
| digest | String | 摘要结果。 |

### 1.4 file.getSavedFileList

`file.getSavedFileList` 是获取保存的所有文件信息的 API。

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否 | 小程序的 appId。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.getSavedFileList({}, result => {
    result && !result.error && console.log(result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| fileList | List | 文件列表。 |

**File 对象属性**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| size | Number | 文件大小。 |
| createTime | Number | 创建时间。 |
| apFilePath | String | 文件路径。 |

### 1.5 file.removeSavedFile

`file.removeSavedFile` 是删除某个保存的文件的 API。

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| apFilePath | String | 是 | 文件路径。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.getSavedFileList({}, result => {
    result && !result.error && file.removeSavedFile({
        apFilePath: result.fileList[0].apFilePath,
    }, result => {
        console.log(result);
    });
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| apFilePath | String | 被删除文件的路径。 |

### 1.6 file.readFile

`file.readFile` 是用来读取文件内容的API。

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| filePath | String | 是 | 文件路径。 |

**示例代码**

```javascript
const file = $falcon.jsapi.file;
file.readFile({
    filePath: 'd:\\demo.txt',
}, result => {
    result && !result.error && console.log(result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| content | String | 文件的内容。 |

## 2. File JSAPI使用教程

### 2.1 file对象获取

在使用File JSAPI前，需要先获取 `$falcon.jsapi` 的 `file` 对象，所有的接口都是通过 `file` 对象进行调用的。

```javascript
const storage = $falcon.jsapi.file;
```

### 2.2 文件存储

文件存储操作需要通过步骤1获取的file对象来进行，具体可通过file.saveFile来实现键值对参数的存储，可存储的文件类型有txt、png、jpg、zip等常见类型。

| 文件类型/文件 | 操作 | 写入操作 |
| :--- | :--- | :--- |
| txt | 支持 |
| png | 支持 |
| jpg | 支持 |
| zip | 支持 |
| ... | 支持 |

在进行文件存储时，有两种方式，一种是同步存储，另一种是异步存储。同步存储直接返回存储结果，异步存储需要在async函数中操作，通过await返回存储结果。

| 接口名/参数列表 | 入参1 | 入参2 |
| :--- | :--- | :--- |
| saveFile | apFilePath: 'xxx' | (result) => {console.log(result);} |

**同步存储方式：**

```javascript
// sync
const file = $falcon.jsapi.file;
file.saveFile({
    apFilePath: "d:\\demo.jpg",
}, (result) => {
    console.log(result);
});
```

**异步存储方式：**

```javascript
// async
const file = $falcon.jsapi.file;
let result = await storage.setStorage({
    key: 'key', // key
    data: 'storage content', // value
});
console.log(result);
```

### 2.3 其他文件操作

其他文件操作也需要通过步骤1获取的file对象来进行，包含 `getSavedFileInfo`、`getFileInfo`、`getSavedFileList` 和 `removeSavedFile`，也支持同步和异步两种操作。

## 3. File JSAPI调用示例

本节将以保存一张图片为示例，介绍如何去调用File JSAPI，具体包含保存文件、获取保存的文件信息、获取文件信息、获取保存的文件列表和删除文件。调用saveFile()，就可将文件存储到当前小程序的私有文件路径下，以供小程序使用。文件使用完毕后，调用removeSavedFile()，就可将之删除。

### 3.1 保存文件

从给定 `apFilePath` 中获取要保存的文件路径，并调用 `saveFile()` 将之保存到小程序的私有文件路径下：

```javascript
saveFile() {
    const file = $falcon.jsapi.file;
    file.saveFile({
        apFilePath: "d:\\demo.jpg",
    }, (result) => {
        console.log(result);
    });
}
```

### 3.2 获取保存的文件信息

要获取某个已保存的文件的信息，可通过调用 `getSavedFileInfo()` 实现：

```javascript
getSavedFileInfo() {
    const file = $falcon.jsapi.file;
    file.saveFile({
        apFilePath: "d:\\demo.jpg",
    }, (result) => {
        result && !result.error && file.getSavedFileInfo({
            apFilePath: result["apFilePath"],
        }, (result) => {
            console.log(result);
        });
    });
}
```

### 3.3 获取文件信息

要获取某个文件的信息，可通过调用 `getFileInfo()` 实现：

```javascript
getFileInfo() {
    const file = $falcon.jsapi.file;
    file.getFileInfo({
        apFilePath: "d:\\demo.jpg",
    }, (result) => {
        console.log(result);
    });
}
```

### 3.4 获取保存的文件列表

要获取已保存的文件的列表，可通过调用 `getSavedFileList()` 实现：

```javascript
getSavedFileList() {
    const file = $falcon.jsapi.file;
    file.getSavedFileList({}, (result) => {
        result && !result.error && console.log(result);
    });
}
```

### 3.5 删除文件

删除通过saveFile()保存的文件，可以调用 `removeSavedFile()` 实现：

```javascript
removeSavedFile() {
    const file = $falcon.jsapi.file;
    file.getSavedFileList({}, (result) => {
        result && !result.error && file.removeSavedFile({
            apFilePath: result.fileList[0].apFilePath,
        }, (result) => {
            console.log(result);
        });
    });
}
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 Net-网络(已不更新)</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # Net-网络(已不更新)

## 说明

已不更新，新方案(需要框架V1.5版本)见 [http/s 文档](http/s 文档)

## 示例代码

```javascript
const http = $falcon.jsapi.http

http.request({
    url: 'https://testpmc.youxuepai.com/push/cross/preschool/getUserTracks.json',
    data: {
        'machine_no': '7120935186230152460',
        'nextStart': '',
        'date': '2020-12-09',
        'pageSize': '10',
        'start_date': '2020-12-09'
    },
    header: {
        'Content-Type': 'application/json'
    }
}, (result) => {
    console.log(result);
});

//或者使用await
const result = await http.request(options);
console.log(result);
```

## 参数说明

| 参数   | 类型           | 必填 | 说明                         |
|--------|----------------|------|------------------------------|
| url    | String         | 是   | 开发者服务器接口地址         |
| data   | String, Object | 否   | 请求的参数                   |
| header | Object         | 否   | 设置请求的 header            |
| method | String         | 否   | 默认为 GET，有效值GET, POST  |
        ```
          </div>
        </details>
        <details>
          <summary>📄 Storage-数据存储(已不更新)</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # Storage-数据存储(已不更新)

## 说明
*   value目前只支持字符串类型
*   已不更新，新方案(需要框架V1.5版本)见[新版 Storage 文档](#)

## 1. Storage JSAPI简介
Storage JSAPI是一个轻量级数据存储接口，特别适用于存储应用的各项数据。

例如在天气应用中，在某一次使用时用户添加了若干个城市，当用户下一次点开时也希望之前设置的城市会保存在应用中，以方便直接获取信息。又如在屏保设置选项里，用户设置了打开屏保，当设备下次开机时也需要记住用户的选项，将屏保打开。这时就需要借助于Storage JSAPI，利用它来存储一些键值对(Key-Value)参数。

Storage JSAPI的作用域为当前小程序应用，包含的接口如下：

| 接口宿主 | JSAPI | 调用方法 | 接口功能 |
| :--- | :--- | :--- | :--- |
| storage | setStorage | `$falcon.jsapi.storage.setStorage({key: 'key', data: 'data'})` | 数据存储 |
| | getStorage | `$falcon.jsapi.storage.getStorage({key: 'key'})` | 数据读取 |
| | removeStorage | `$falcon.jsapi.storage.removeStorage({key: 'key'})` | 数据删除 |
| | clearStorage | `$falcon.jsapi.storage.clearStorage()` | 数据清空 |

### 1.1 setStorage
保存 key-value 数据

**入参**
Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| key | String | 是 | 缓存数据的 key。 |
| data | String | 是 | 缓存数据的 value。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**
```javascript
// 异步方法
const storage = $falcon.jsapi.storage;
storage.setStorage({
    key: 'key', // key
    data: 'storage content', // value
}, result => {
    result && !result.error && console.log('save success: ', result);
});

// 同步方法
const storage = $falcon.jsapi.storage;
let result = await storage.setStorage({
    key: 'key', // key
    data: 'storage content', // value
});
result && !result.error && console.log('save success: ', result);
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| key | String | 缓存数据的 key。 |
| data | String | 缓存数据的 value。 |

### 1.2 getStorage
读取key-value 数据

**入参**
Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| key | String | 是 | 缓存数据的 key。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**
```javascript
const storage = $falcon.jsapi.storage;
storage.getStorage({
    key: 'key' // key
}, result => {
    result && !result.error && console.log('dialog clicked ', result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| data | String | key 对应的内容。 |

### 1.3 removeStorage
删除当前小程序指定key所对应的value数据

**入参**
Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| key | String | 是 | 缓存数据的 key。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**
```javascript
const storage = $falcon.jsapi.storage;
storage.removeStorage({
    key: 'key' // key
}, result => {
    result && !result.error && console.log('dialog clicked ', result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| key | String | 缓存数据的 key。 |

### 1.4 clearStorage
清空当前小程序所有的key-value数据

**入参**
Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否 | 小程序的 appId。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**
```javascript
const storage = $falcon.jsapi.storage;
storage.clearStorage({}, result => {
    result && !result.error && console.log('dialog clicked ', result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| appId | String | 被清空数据的小程序的 id。 |

### 1.5 getStorageInfo
获取当前小程序所有的 key数据，将返回所有通过setStorage接口存储的key数据

**入参**
Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否 | 小程序的 appId。 |
| callback | Function | 否 | 回调函数，包含result参数。 |

**示例代码**
```javascript
const storage = $falcon.jsapi.storage;
storage.getStorageInfo({}, result => {
    result && !result.error && console.log('dialog clicked ', result);
});
```

**success 回调函数**

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| keys | String Array | 当前 storage 中所有的 key。 |
| currentSize | Number | 当前占用的空间大小, 单位为 KB。 |
| limitSize | Number | 限制的空间大小，单位为 KB。 |

## 2. Storage JSAPI使用教程

### 2.1 storage 对象获取
在使用Storage JSAPI前，需要先获取 `$falcon.jsapi` 的 `storage`对象，所有的接口都是通过 `storage`对象进行调用的。

```javascript
const storage = $falcon.jsapi.storage;
```

### 2.2 数据存储
数据存储操作需要通过步骤1获取的storage对象来进行，具体可通过`storage.setStorage`来实现键值对参数的存储，可存储的数据类型有Bool、Float、Int、Long、String五种。

| 数据类型/数据操作 | 写入操作 |
| :--- | :--- |
| Bool | 支持 |
| Float | 支持 |
| Int | 支持 |
| Long | 支持 |
| String | 支持 |

在进行数据存储时，有两种方式，一种是同步存储，另一种是异步存储。同步存储直接返回存储结果，通过await返回存储结果，异步存储需要在async函数中操作。

| 接口名/参数列表 | 入参1 | 入参2 |
| :--- | :--- | :--- |
| setStorage | key: 'key' | data: 'storage content' |

**异步存储方式：**
```javascript
// async
const storage = $falcon.jsapi.storage;
storage.setStorage({
    key: 'key', // key
    data: 'storage content', // value
}, result => {});
```

**同步存储方式：**
```javascript
// sync
const storage = $falcon.jsapi.storage;
let result = await storage.setStorage({
    key: 'key', // key
    data: 'storage content', // value
});
console.log(result);
```

### 2.3 数据读取
数据读取操作也需要通过步骤1获取的storage对象来进行，具体可通过`storage.getStorage`来实现键值对参数的读取，可读取的数据类型有Bool、Float、Int、Long、String五种。

| 数据类型/数据操作 | 读取操作 |
| :--- | :--- |
| Bool | 支持 |
| Float | 支持 |
| Int | 支持 |
| Long | 支持 |
| String | 支持 |

在进行数据读取时，也有两种方式，一种是同步读取，另一种是异步读取。同步读取直接返回读取结果，通过await返回读取结果，异步读取需要在async函数中操作。

| 接口名/参数列表 | 入参1 |
| :--- | :--- |
| getStorage | key: 'key' |

**异步读取方式：**
```javascript
// async
const storage = $falcon.jsapi.storage;
storage.getStorage({
    key: 'key' // key
}, result => {
    result && !result.error && console.log('dialog clicked ', result);
});
```

**同步读取方式：**
```javascript
// sync
const storage = $falcon.jsapi.storage;
let result = await storage.getStorage({
    key: 'key', // key
});
console.log(result);
```

## 3. Storage JSAPI调用示例
本节将以屏保设置选项为示例，介绍如何去调用 Storage JSAPI。屏保设置选项是指用户是否设置了开启屏保，用户在设置了屏保选项后，设备在下次开机后，应保持用户的设置，根据用户的设置默认将屏保打开或关闭。用户设置了屏保开关的选项后，这个设置参数就可以通过 Storage JSAPI存储到磁盘中，下次设备开机就会主动去读取该选项，这样就做到了用户设置的持久化。

### 3.1 屏保设置页面开发
首先需要开发屏保设置页面，`<template>`模板和`<style>`样式如下：

```html
<template>
    <div class="wrapper">
        <text class="screen" @click="open">屏保开启</text>
        <FlSwitch @change="switchChange" :width="100" :height="50" colorChecked="blue" v-model="switchChecked" />
    </div>
</template>
<style scoped>
    .wrapper {
        justify-content: center;
        align-items: center;
        background-color: #f8f8ff;
    }
    .screen {
        margin-bottom: 20px;
        padding: 10px;
        background-color: #888;
        border-radius: 10px;
        align-items: center;
        justify-content: center;
        font-size: 30px;
    }
</style>
```

### 3.2 保存屏保设置参数
用户设置的屏保是否开启的参数可通过Storage JSAPI中的setstorage接口进行保存，当用户选择打开屏保时，将screen-saver参数设置为on，当用户选择关闭屏保时，将screen-saver参数设置为off。

setstorage设置 screen-saver参数的操作在 FlSwitch组件的状态发生改变时进行，FlSwitch组件的状态通过 switchChange函数进行监听，具体`<script>`脚本如下：

```javascript
<script>
    import FlSwitch from "../../packages/switch/index.vue";
    export default {
        name: "index",
        components: {
            FlSwitch,
        },
        data() {
            return {
                msg: "",
            };
        },
        methods: {
            switchChange(on) {
                console.log("switchChange ", on);
                const storage = $falcon.jsapi.storage;
                if (on == true) {
                    storage.setStorage({
                        key: 'screen-saver', // key
                        data: 'on', // value
                    }, result => {});
                } else {
                    storage.setStorage({
                        key: 'screen-saver', // key
                        data: 'off', // value
                    }, result => {});
                }
            },
            finishApp() {
                this.$app.finish();
            },
            finishPage() {
                this.$page.finish();
            },
        },
    };
</script>
```

### 3.3 屏保设置效果展示
当用户设置开启屏保时，screen-saver的值被置为on；当用户设置关闭屏保时，screen-saver的值被置为off。
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 系统级别JSAPI</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary><strong>📁 多媒体</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 mediaplayer-音频播放</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # mediaplayer-音频播放

## 1. 概述
提供音频、视频播放能力，控制音视频播放、停止、快进、快退，监听播放状态信息和播放错误信息。

参考：https://developer.android.google.cn/reference/android/media/MediaPlayer?hl=zh-cn

目前mediaplayer该版本的接口都为异步接口，后续会有提供同步的音频播放模块。

## 2. 模块使用方式

```javascript
import mediaplayer from 'mediaplayer'
```

## 3. 方法

### 3.1 播放流程

#### 3.1.1 setDataSource()

**功能**
- 设置播放文件地址

**参数**
- path

| 属性 | 类型   | 必填 | 描述                                 |
|------|--------|------|--------------------------------------|
| path | String | 是   | url或者本地路径 ，本地路径需加fs: 前缀，具体看用法。 |

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
// 播放网络资源
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
// 播放本地资源
await this.player.setDataSource("fs:/customer/audio/liehuxingzuo.mp3");
```

#### 3.1.2 start()

**功能**
- 开始播放

**参数**
- 无

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
```

#### 3.1.3 pause()

**功能**
- 播放暂停，需要在播放时调用

**参数**
- 无

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
await this.player.pause();
```

#### 3.1.4 resume()

**功能**
- 从暂停态恢复播放状态，与pause一一对应

**参数**
- 无

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
await this.player.pause();
await this.player.resume();
```

#### 3.1.5 seekTo()

**功能**
- 跳转到指定位置

**参数**
- pos

| 属性 | 类型 | 必填 | 描述                           |
|------|------|------|--------------------------------|
| pos  | long | 是   | 需要跳转的时间点,单位是s |

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
await this.player.seekTo(20);
```

#### 3.1.6 stop()

**功能**
- 停止播放，停止播放后不能继续播放，必须重新调用setDataSource。stop操作停止当前播放，并释放文件资源，播放器状态reset。

**参数**
- 无

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
await this.player.stop();
```

#### 3.1.7 release()

**功能**
- 释放播放器资源，在当前player不再使用时调用，该方法会先stop停止播放，再释放player资源。使用时需要与创建对象一一对应。

**参数**
- 无

**返回值**
- 成功返回0，错误抛出异常

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
await this.player.release();
```

### 3.2 播放信息获取

#### 3.2.1 getDuration()

**功能**
- 返回当前文件的时长，单位s

**参数**
- 无

**返回值**
- duration: 播放文件的时长

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
var duration = await this.player.getDuration();
```

#### 3.2.2 getPosition()

**功能**
- 返回当前的播放位置，单位s

**参数**
- 无

**返回值**
- position: 当前的播放位置

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
var position = await this.player.getPosition();
```

#### 3.2.3 isPlaying()

**功能**
- 查询当前播放器是否处于播放状态

**参数**
- 无

**返回值**
- res: true: 正在播放; false: 未播放。

**用法**
```javascript
this.player = new media.MediaPlayer();
await this.player.setDataSource("http://192.168.3.182/file/test.mp3");
await this.player.start();
var isPlaying = await this.player.isPlaying();
```

## 4. 事件

### 4.1 state

**功能**
- 监听播放器播放状态

**参数**
- topic: "state"

| 属性  | 类型   | 必填 | 描述      |
|-------|--------|------|-----------|
| topic | String | 是   | "state" |

**返回值**
- object对象
    - status：播放器状态，详情见列表

| status | 状态描述       |
|--------|----------------|
| start  | 开始播放       |
| pause  | 暂停播放       |
| resume | 重新开始播放   |
| stop   | 停止播放       |
| finish | 结束播放       |

**用法**
```javascript
this.player = new media.MediaPlayer();
this.player.on("state", (res) => {
    console.log(" this.player.on state " + JSON.stringify(res.status));
});
```

### 4.2 error

**功能**
- 监听播放器异常信息

**参数**
- topic: "error"

**返回值**
- object 对象
    - errMsg：错误信息
    - errorCode：  错误码

| errMsg                              | errorCode |
|-------------------------------------|-----------|
| PLAYER_ERROR_UNKNOWN（未知错误）    | 8010901   |
| PLAYER_ERR_FORMAT_UNSUPPORT(播放格式不支持) | 8010902   |

**用法**
```javascript
this.player = new media.MediaPlayer();
this.player.on("error", (res) => {
    console.log(" this.player.on error " + JSON.stringify(res));
});
```
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary><strong>📁 容器框架服务</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 am-应用栈管理</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # am-应用栈管理

## 1. 概述
提供应用栈的管理机制。

## 2. 模块使用方式

```javascript
import am from 'am'
```

### 2.1 应用预加载
为支持应用在开机启动时预加载，通过如下app.json配置（系统级应用生效），会在启动home前预启动该app。

```javascript
{
  "props": {
    "Preload": true
  }
}
```

## 3. 方法

### 3.1 getTopApp()
获取栈顶appid。

**入参**
无

**调用示例**
```javascript
import am from '$am';
let topApp = am.getTopApp();
```

### 3.2 moveToBack
将app在应用栈中下移，隐藏到后台（还在应用栈中）。

**权限**
移动本应用可授权，移动其他应用需系统应用权限。

**入参**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否(为空表示移动本应用) | 要移动的应用的appId。 |

**调用示例**
```javascript
import am from '$am';
am.moveToBack();
```

### 3.3 hide
将app移动到隐藏应用栈中，其他app退栈不会自动显示该app（需通过startApp重新加入）。

**权限**
移动本应用可授权，移动其他应用需系统应用权限。

**入参**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否(为空表示移动本应用) | 要移动的应用的appId。 |

**调用示例**
```javascript
import am from '$am';
am.hide();
```

### 3.4 closeApp
关闭指定app。

**权限**
关闭本应用可授权，关闭其他应用需系统应用权限。

**入参**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appId | String | 否(为空表示移动本应用) | 要移动的应用的appId。 |
| forceFinish | bool | 否 | 是否强制退出，为false时，如果app存在持久化后台服务，app不会退出，只会界面退出。 |

**调用示例**
```javascript
import am from '$am';
am.closeApp(undefined, false);
```

### 3.5 hasWindowFocus
本应用是否具有焦点窗口。

**入参**
无

**调用示例**
```javascript
import am from '$am';
let focused = am.hasWindowFocus();
```

### 3.6 isAttachedToWindow
本应用当前是否有界面打开。

**入参**
无

**调用示例**
```javascript
import am from '$am';
let attached = am.isAttachedToWindow();
```

## 4. 事件

### 4.1 topApp
栈顶应用变化。

**调用示例**
```javascript
import am from '$am';
am.on('topApp', r => {});
```

### 4.2 windowAttachState
本应用界面开启关闭事件。

**调用示例**
```javascript
import am from '$am';
am.on('windowAttachState', r => {});
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 nav-页面导航</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # nav-页面导航

## 1. 概述
页面/应用跳转相关 API，支持应用内、应用间、以及 Category 跳转；目前依托于全局的 `falcon` 对象 `$falcon`。

`$falcon` 下的接口均为全局接口。

## 2. 方法

### 2.1 `$falcon.navTo()`
页面/应用跳转。跳转到应用内指定页面或者其他应用的指定页面。

**方法原型:**
```javascript
$falcon.navTo(target: String, options: Object)
```

**参数：**
*   **target**: 需要跳转的页面名称或目标页面的 uri。
    *   **应用内跳转**：参数为**页面名称**。如：`"index"`, `"xxPage"`。
    *   **应用间跳转**：参数为 uri，如：`"falcon://appid/index?param1=xxx"`。schema 必须为 `falcon`。
    *   **Category 跳转**：参数同**应用间跳转**，如 `"falcon://{Category}/index?param1=xxx"`，具体 Category 如何配置参考 `local_packages.json` 中的 amr 包配置（请确保配置 Category 的唯一性）。
*   **options**: 页面参数，跳转到下一个页面所需要的参数。如果 target 为 uri 且带有参数，则会合并后传给下一个页面；参数格式为 key/value 字符串 JSON 格式。
    *   KV 格式：如 `{ data: "data1" }`。

**用法**
```javascript
$falcon.navTo("pageName", { data: "data1" }); // 应用内页面跳转，pageName为应用页面名
$falcon.navTo("falcon://HOME/index"); // Category跳转，且指定index页面
$falcon.navTo("falcon://8180000000000020"); // 不推荐通过appid进行，应用间跳转，默认跳转到index页面
$falcon.navTo("falcon://8180000000000020/index"); // 不推荐通过appid进行，应用间跳转，且指定index页面

// 新页面page.js在onLoad生命周期中接收参数
onLoad(options) { }
```

### 2.2 `$falcon.closeApp()`
应用退出，一般用于应用不需要在后台的时候或者需要销毁的时候执行，**系统级应用才可有权限执行**。

**用法**
```javascript
$falcon.closeApp()
```

### 2.3 `$falcon.closePageByName()`
根据页面名称关闭页面，**系统级应用才可有权限执行**。

**参数**
*   **pageName**：页面名；页面名称可通过对应页面实例的 `$pageName` 属性获取。

**用法**
```javascript
$falcon.closePageByName(pageName: String)
```

### 2.4 `$falcon.closePageById`
根据页面 id 关闭页面，**系统级应用才可有权限执行**。

**参数**
*   **pageId**：页面 id。页面 id 可通过对应页面实例的 `$pageId` 属性获取。

**用法**
```javascript
$falcon.closePageById(pageId: String)
```

**关键字**
页面跳转、路由
            ```
              </div>
            </details>
            <details>
              <summary>📄 pm-应用包管理</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # pm-应用包管理

## 1. 概述

pm模块用于本地amr包的管理，包括安装、卸载amr包，获取指定/所有amr包的安装信息，监听amr包安装、卸载和升级的事件。

## 2. 模块使用方式

```javascript
import pm from 'pm';
```

## 3. 方法

### 3.1 installPackage

安装amr应用包(框架打包工具的后缀统一为.amr)

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
|------|------|------|------|
| path | String | 是 | amr的绝对安装路径。 |
| callback | function | 否 | 回调函数。 |

**callback中的状态码**

```javascript
SUCCESS = 0, /* Success */
INS_ERROR_FILE_NOT_EXIST = 2,
INS_ERROR_FAILED_INVALID_AMR = 3,
```

**调用示例**

```javascript
import pm from 'pm';
pm.installPackage(path, r => {
    if (r.res === pm.SUCCESS) {
        console.log('install succeess')
    } else {
        console.log('install failed, error code: ', r.res)
    }
});
```

### 3.2 removePackage

卸载amr包

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
|------|------|------|------|
| appId | String | 是 | 要删除的应用的appId。 |
| callback | function | 否 | 回调函数。 |

**callback中的状态码**

```javascript
SUCCESS = 0, /* Success */
RM_ERROR_NOT_EXIST = 50,
RM_ERROR_NOT_ALLOWED = 51,
```

**调用示例**

```javascript
import pm from 'pm';
pm.removePackage(appId, r => {
    if (r.res === pm.SUCCESS) {
        console.log('remove succeess')
    } else {
        console.log('remove failed, error code: ', r.res)
    }
});
```

### 3.3 getPackageInfo

获取指定appId的安装信息

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
|------|------|------|------|
| appId | String | 是 | 要获取安装信息的应用的appId。 |

**调用示例**

```javascript
import pm from 'pm';
let packageInfo = pm.getPackageInfo(appId);
console.log(packageInfo)
```

**返回值**

object

```json
{
    "appid": "8001000000000001",
    "name": "app1",
    "version": "1.0.0",
    "icon": "",
    "installPath": "/etc/miniapp/data/mini_app/pkgs/8001000000000001/a/",
    "flag": 1
}
```

### 3.4 getInstalledPackages

获取安装包的安装信息

**入参**

Object 类型，属性如下：

| 属性 | 类型 | 必填 | 描述 |
|------|------|------|------|
| 无 | | | |

**调用示例**

```javascript
import pm from 'pm';
let packageInfos = pm.getInstalledPackages();
console.log(packageInfos)
```

**返回值**

array

```json
[
    {
        "appid": "8001000000000001",
        "name": "app1",
        "version": "1.0.0",
        "icon": "",
        "installPath": "/etc/miniapp/data/mini_app/pkgs/8001000000000001/a/",
        "flag": 1
    },
    {
        "appid": "8001000000000002",
        "name": "app2",
        "version": "1.0.0",
        "icon": "",
        "installPath": "/etc/miniapp/data/mini_app/pkgs/8001000000000002/a/",
        "flag": 1
    }
]
```

### 3.5 package事件监听

| 事件类型 | 事件名称 | 描述 |
|----------|----------|------|
| installed | amr安装事件 | |
| removed | amr删除事件 | |
| updated | amr更新事件 | |

**调用示例**

```javascript
// 监听
let token = pm.on('package', r => {
    if (r.type === 'installed') {
    } else if (r.type === 'removed') {
    } else if (r.type === 'updated') {
    }
});
// 取消监听
pm.off(token);
```

## 安装失败

| 错误码 | 说明 |
|--------|------|
| 1 | 安装失败，amr包不存在 |
| 2 | 安装失败，amr包解析失败 |
| 3 | 安装失败，解压失败 |
            ```
              </div>
            </details>
            <details>
              <summary>📄 updater-应用升级</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # updater-应用升级

## 1. 概述
updater 模块用于检查应用升级情况，下载应用，升级应用。

## 2. 模块使用方式

```javascript
import updater from 'updater'
```

## 3. 方法

### 3.1 getUpdateInfo
检查应用升级情况。

**入参**
异步接口：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appid | String | 是 | 待检查appid。 |
| callback | Function | 否 | 检查后的回调函数。 |

**callback参数：**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| checkResult | Object | 否 | 检查结果 |
| state | int | 是 | 当前状态 |

**state 列表：**

| 值 | 含义 |
| :--- | :--- |
| ST_UP_TO_DATE | 稳态，没有新的版本 |
| ST_CHECK_PENDING | 中间态，正在检查版本 |
| ST_HAS_NEW_VERSION | 稳态，有新的版本 |
| ST_DOWNLOADING | 中间态，正在下载 |
| ST_DOWNLOAD_DONE | 稳态，下载完成等待安装 |
| ST_INSTALLING | 中间态，正在安装 |
| ST_DOWNLOAD_PAUSED | 稳态，下载已暂停 |

```javascript
// 异步方法
updater.getUpdateInfo(appid, (updateInfo) => {
    console.log(`#######>> appid updateInfo ${JSON.stringify(updateInfo)}`)
    if (updateInfo.status === updater.ST_HAS_NEW_VERSION) {
        console.log(`可更新版本: ${updateInfo.checkResult.version} ${updateInfo.checkResult.desc}`)
    }
})
```

### 3.2 startDownload
下载应用包。

**入参**
异步接口：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appid | String | 是 | 带下载包appid |
| callback | Function | 否 | 回调函数 |

**callback参数：**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| result | int | 是 | 下载结果 |
| reason | int | 是 | 下载终止或暂停时对应的错误原因，见DownloadReason |

**DownloadResult 列表：**

| 属性名 | 含义 |
| :--- | :--- |
| DL_ERROR | Deprecated 同 DL_ABORTED |
| DL_ABORTED | 下载终止 |
| DL_SUCCESS | 下载完成 |
| DL_ERROR_ON_GOING | 已经在下载 |
| DL_PAUSED | 下载暂停 |

**DownloadReason 列表**

| 属性名 | 含义 |
| :--- | :--- |
| DL_REASON_ABORT_INIT_ERROR | 下载初始化失败导致下载终止 |
| DL_REASON_ABORT_URL_INACCESSIBLE | 下载地址不可访问导致下载终止 |
| DL_REASON_ABORT_MD5_MISMATCH | 下载文件的MD5与预期结果不匹配导致下载终止 |
| DL_REASON_ABORT_UNKNOWN | 未知原因导致下载终止 |
| DL_REASON_PAUSE_NETWORK_BROKEN | 网络中断导致下载暂停 |
| DL_REASON_PAUSE_INSUFFICIENT_SPACE | 系统空间不足导致下载暂停 |
| DL_REASON_PAUSE_IO_EXCEPTION | 内部存储读写错误导致下载暂停 |
| DL_REASON_PAUSE_UNKNOWN | 未知原因导致下载暂停 |

```javascript
// 异步接口
updater.startDownload(appInfo.appid, (info) => {
    console.log(`download cb ${JSON.stringify(info)}`)
    if (info.result === updater.DL_SUCCESS) {
        console.log('下载成功')
    } else if (info.result === updater.DL_PAUSED) {
        if (info.reason === updater.DL_REASON_PAUSE_NETWORK_BROKEN) {
            console.log('网络连接不佳，升级包下载已暂停')
        }
    }
})
```

### 3.3 installUpdate
安装已下载好的应用包。

**入参**
异步接口：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| appid | String | 是 | 待安装的appid |
| callback | Function | 否 | 回调函数 |

**callback参数：**

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| result | int | 是 | 安装结果 |

**InstallResult 列表：**

| 属性名 | 含义 |
| :--- | :--- |
| INS_ERROR_INSTALLATION | 安装过程失败 |
| INS_ERROR_NO_INSTALL_PACKAGE | 没有已下载的更新包 |
| INS_ERROR_INSTALL_ALREADY_ON_GOING | 已经在安装，重复调用 |
| INS_ERROR_NO_UPDATE_INFO | 没有此appid的更新上下文 |
| INS_SUCCESS | 安装成功 |

```javascript
// 异步接口
updater.installUpdate(appInfo.appid, (info) => {
    console.log(`installUpdate cb ${JSON.stringify(info)}`)
    if (info.result === updater.INS_SUCCESS) {
        console.log('安装成功')
    }
})
```

### 3.4 on 事件

#### 升级模块就绪事件
当升级模块准备就绪后，即触发此事件。
此外，当应用注册此事件监听时、升级模块已经处于就绪状态，则此回调会被立即调用。
注意：升级模块就绪前，调用 getUpdateInfo 会直接回调失败。

| 事件名 | ready |
| :--- | :--- |
| 回调函数入参 | 无 |

```javascript
updater.on("ready", () => {
    console.log("Updater is ready")
})
```

#### 更新模块状态事件

**回调函数入参**
回调函数：

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| topic | String | 是 | 监听事件类型 |
| callback | Function | 是 | 监听回调 |

**GloablEvent 列表：**

| 属性名 | 含义 |
| :--- | :--- |
| EVT_UPDATE_CHECK_PENDING | 正在检查版本 |
| EVT_UPDATE_CHECK_ERROR | 版本检查错误 |
| EVT_GOT_ALREADY_UP_TO_DATE | 没有新版本 |
| EVT_GOT_NEW_VERSION | 获取到新版本 |
| EVT_DOWNLOAD_PENDING | 触发下载 |
| EVT_DOWNLOAD_PERCENT_CHANGE | 下载进度变化 |
| EVT_DOWNLOAD_ERROR | 下载错误终止 |
| EVT_DOWNLOAD_DONE | 下载完成 |
| EVT_INSTALLING | 正在安装 |
| EVT_INSTALL_SUCCESS | 安装成功 |
| EVT_INSTALL_ERROR | 安装失败 |
| EVT_DOWNLOAD_PAUSED | 下载暂停 |

```javascript
updater.on('updateInfo', ({event, info}) => {
    console.log(`updateInfo ${event} ${JSON.stringify(info)}`)
    // event: GlobalEvent
    // info: UpdateInfo
    if (event == updater.EVT_DOWNLOAD_PERCENT_CHANGE) {
        console.log(`应用安装进度： ${info.appid} ${info.downloadPercent}`)
    }
})
```

### 3.5 isReady
判断升级模块是否就绪。
注意：升级模块就绪前，调用 getUpdateInfo 会直接回调失败。

**参数**
无

**返回值**
类型：bool
true：已就绪
false：未就绪

```javascript
if (updater.isReady()) {
    console.log("Updater is ready")
} else {
    console.log("Updater is NOT ready")
}
```

## icon配置
目前支持用相对路径和绝对路径，在vue里面可通过pm.getPackageInfo去获取amr的安装路径去加载图片，打包的图片都在amr的安装路径下面。
            ```
              </div>
            </details>
            <details>
              <summary>📄 应用和页面管理</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # 应用和页面管理

## 1. 概述
应用和页面的退出方案，合理退出可以有效降低设备的内存资源的占用。

## 2. App
App的方法不是全局的，是应用对应的App类实例化后的方法。该实例可通过 `$falcon.$app` 属性获取。

### 2.1 app.finish()
**用法**
退出当前应用，应用退到后台时调用。

```javascript
$falcon.$app.finish();
// 可以通过$falcon全局对象获取当前应用App对象的实例
```

## 3. Page
Page的方法不是全局的，只在页面生命周期内以及页面的根组件和子组件中可访问。
组件中可通过在组件中 `this.$page` 或者 `$falcon.getPage(component:Component)` 获取当前页面的引用。

### 3.1 page.finish
**用法**
关闭当前页面。

```javascript
this.$page.finish();
```

### 3.2 page.setRootComponent
设置页面的根组件。每个页面都需要对应一个根组件。此方法在页面的onLoad生命周期中调用。

**方法原型:**
```javascript
page.setRootComponent(component:Component)
```

**参数说明:**
- `component`: Vue组件对象

```javascript
import IndexComponent from './index.vue';

class PageIndex extends $falcon.Page {
    onLoad(options) {
        super.onLoad(options);
        this.setRootComponent(IndexComponent);
    }
}

export default PageIndex;
```
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary><strong>📁 文件&加解密</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 crypto-加解密</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # crypto-加解密

## 1. 概述
crypto 提供加解密算法，如哈希计算等。

## 2. 模块使用方式

```javascript
import crypto from 'crypto'
```

## 3. 方法

### 3.1 Hash构造函数

**参数：**
- `hashMethod`： hash的方法，可选值目前只有 `md5`

**返回值：**
- `Hash` 对象

### 3.2 Hash.hashFile接口

**参数**
- `path`: 文件路径

**返回值**
- 返回hash值，具体的hash方法由 `crypto.Hash` 构造函数决定

**用法**
对某个文件计算 md5 值

```javascript
const md5 = new crypto.Hash('md5')
const fpath = '/etc/resolv.conf'
const hexdigest = await md5.hashFile(fpath)
console.log(`hash file ${fpath}, md5: ${hexdigest}`)
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 fs-文件操作</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # fs-文件操作

## 概述
fs 提供文件操作接口（仅支持当前应用路的data目录操作）。

## 2. 模块使用方式

```javascript
import fs from 'fs'
```

## 3. 方法

### 3.1 readdir 接口

**参数**
*   `path`: 读取的目录路径
*   `options`：`{withFileTypes: true}`

**返回值**
*   如果 `withFileTypes` 为 `false` （默认）则返回文件名称列表
*   如果 `withFileTypes` 为 `true` 则返回详细信息 `fs.Dirent` 列表

**用法**
读取某路径下所有文件名

```javascript
const flist = await fs.readdir('.')
for (let fname of flist) {
  console.log(`fname ${fname}`)
}
console.log(`== dirent case ==`)
const dirents = await fs.readdir('.', { withFileTypes: true })
for (let dirent of dirents) {
  console.log(`fname ${dirent.name} isFile ${dirent.isFile()} isDir ${dirent.isDirectory()}`)
}
```

### 3.2 Dirent 结构体

**成员：**
*   `Dirent.name` 文件名称

**方法：**
*   `isDirectory()` 目录
*   `isFile()` 文件

### 3.3 stat 接口

**参数**
*   `path`：文件路径

**返回值**
*   `stat` 对象，字段与 linux struct stat 一致，见 3.4 stat 结构体

**用法**
读取某路径的stat 信息

```javascript
const stat = await fs.stat('.')
console.log(`stat . ${JSON.stringify(stat)}`)
```

### 3.4 stat 结构体

**成员：**
*   `size`：file size, in bytes
*   `atimeMs`：time of last access
*   `mtimeMs`：time of last data modification
*   `birthtimeMs`：time of file creation(birth)

### 3.5 exists 接口

**参数**
*   `path`：文件路径

**返回值**
*   存在返回 `true`，不存在返回 `false`

**用法**
判断某路径是否存在

```javascript
const fpath = "/etc/resolv.conf"
const ret = await fs.exists(fpath)
console.log(`fs.exists ret ${ret}`)
```

### 3.6 readFile 接口

**参数**
*   `path`：文件路径

**返回值**
*   返回文件内容（目前支持读取文本文件）

**用法**
读取某个文件

```javascript
const fpath = "/etc/resolv.conf"
const ret = await fs.readFile(fpath)
console.log(`fs.readFile ret ${ret}`)
```

### 3.7 mkdir 接口

**参数**
*   `path`：文件路径，可递归创建

**返回值**
*   成功返回 `true`，失败返回 `false`

**用法**
递归创建文件夹

```javascript
const dir0 = './__test_fs_dir'
const dir1 = './__test_fs_dir/_inner'
let ret = await fs.mkdir(dir1)
console.log(`mkdir dir1 ret: ${ret}`)
ret = await fs.exists(dir0)
console.log(`exists dir0 ret: ${ret}`)
```

### 3.8 rm 接口

**参数**
*   `path`：文件路径，可递归删除文件、文件夹

**返回值**
*   成功返回 `true`，失败返回 `false`

**用法**
删除文件夹或者文件

```javascript
const dir0 = './__test_fs_dir'
const dir1 = './__test_fs_dir/_inner'
ret = await fs.rm(dir0)
console.log(`rm dir0 ret: ${ret}`)
ret = await fs.exists(dir1)
console.log(`exists dir1 ret: ${ret}`)
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 sqlite-数据库</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # sqlite-数据库

## 1. 概述
sqlite3数据库操作，执行sql语句，返回数据和执行结果。

部分参考：
* node-sqlite3 接口 (https://github.com/TryGhost/node-sqlite3/wiki/API)，比如 all、run、bind等。
* qjs-sqlite3 接口 (https://github.com/ratboy666/qjs-sqlite3)，比如 step 等。

## 2. 模块使用方式

```javascript
import sqlite3 from 'sqlite3'
```

## 3. 方法

### 3.1 open()
**参数**
* `path` 数据库路径

**返回值**
* `Promise`，成功返回0，错误返回错误信息

**用法：**
打开一个数据库时调用。

```javascript
let db = new sqlite3.Database()
await db.open('tstSqlite3.db')
```

### 3.2 close()
**参数**
* 无

**返回值**
* `Promise`，只有成功，返回"OK"表示关闭操作有效，"CLOSED"表示数据库已关闭。

**用法：**
关闭数据库。

```javascript
await db.close()
```

### 3.3 exec()
**参数**
* `sql`：待执行的sql语句

**返回值**
* `Promise`，成功返回0，错误返回错误信息

**用法：**
执行sql语句。

```javascript
let sql = `CREATE TABLE IF NOT EXISTS tst_table(
ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
FILE_ID   TEXT    NOT NULL,
DIR       TEXT    NOT NULL,
NAME      TEXT    NOT NULL
);`
await db.exec(sql)
```

### 3.4 prepare()
**参数**
* `sql`：待编译的sql语句

**返回值**
* `Promise`，成功返回 Statement 对象，错误返回错误消息

**用法：**
编译sql语句，生成 Statement 对象，供后续绑定参数或执行sql使用。

```javascript
let sql = `INSERT INTO tst_table (ID,FILE_ID,DIR,NAME) VALUES (?, ?, ?, ? );`
let st = await db.prepare(sql)
```

### 3.5 path
**属性值**
**返回值**
* 当前数据库路径

**用法：**
获取当前数据库路径。

```javascript
console.log(db.path)
```

### 3.6 Statement.columnNameList()
**参数**
* 无

**返回值**
* `Promise`，返回string列表

**用法：**
获取当前 Statement 的列名称列表。

```javascript
let columnNameList = await st.columnNameList()
```

### 3.7 Statement.bindParameterList()
**参数**
* 无

**返回值**
* `Promise`，返回string列表

**用法：**
获取当前 Statement 绑定参数的名称列表。

```javascript
let bindParameterList = await st.bindParameterList()
```

### 3.8 Statement.bind()
**参数**
* 支持几种入参：
    * 参数列表，用于绑定参数列表
    * 数组，用于绑定参数列表
    * 字典，用于绑定具名参数值

**返回值**
* `Promise`，返回string列表

**用法：**
绑定参数值。

```javascript
let res
res = await st.bind(1, 'fid01', 'dir01', 'name01')
res = await st.bind({':fid': 'fid01'})
res = await st.bind(['fid01'])
```

### 3.9 Statement.step()
**参数**
* 无

**返回值**
* `Promise`
    * 如果是 select 语句返回行数据
    * 已完成所有执行时返回 null
    * 错误返回错误信息

**用法：**
执行 Statement 对应的 sql，一般在bind参数值后调用。

```javascript
let res = await st.step()
```

### 3.10 Statement.run()
**参数**
* 无

**返回值**
* `Promise`，成功返回0，失败返回错误信息

**用法：**
执行 Statement 对应的 sql，完成所有行的执行直到完成状态。

```javascript
let res = await st.run()
```

### 3.11 Statement.all()
**参数**
* 无

**返回值**
* `Promise`，成功返回所有select行数据（如果非select则返回空数组），错误返回错误信息

**用法：**
执行 Statement 对应的 sql，直到完成状态，并返回所有行数据。

```javascript
let res = await st.all()
```

### 3.12 Statement.reset()
**参数**
* 无

**返回值**
* `Promise`，成功返回0，错误返回错误信息

**用法：**
重置 Statement 状态，使它变为可重新执行状态。

```javascript
let res = await st.reset()
```

### 3.13 Statement.clearBindings()
**参数**
* 无

**返回值**
* `Promise`，成功返回0，失败返回错误信息

**用法：**
重置 Statement 绑定参数值，可以再次绑定新的值使用。

```javascript
let res = await st.clearBindings()
```

### 3.14 Statement.finalize()
**参数**
* 无

**返回值**
* `Promise`，成功返回0，重复释放返回“FINALIZED”，失败返回错误信息

**用法：**
销毁 Statement，调用改 Statement 资源被释放，所有成员方法不能再次调用。

```javascript
let res = await st.finalize()
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 storage-kv存储</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # storage-kv存储

## 1. 概述

storage 模块用来存储键值对kv(Key-Value)数据，放在应用路径下面(有别于系统级别的 `system_kv`)。

> **Tip：** 框架V1.5版本以上支持该API

## 2. 模块使用方式

```javascript
import storage from 'storage'
```

## 3. 方法

### 3.1 getStorage()

**参数**
*   `key`：获取的 key 值

**返回值**
*   异步方法，返回对应的 value 值（字符串），失败抛出异常，空值返回null

**用法：**
根据 key 获取某个 value 值

```javascript
let val1 = await storage.getStorage('key1')
let val2 = await storage.getStorage('key2')
this.message = `key1: ${val1}, key2: ${val2}`
```

### 3.2 setStorage()

**参数**
*   `key`：存储的 key 值，必须是字符串
*   `value`：存储的 value 值，必须是字符串

**返回值**
*   异步方法，返回0为成功，失败抛出异常

**用法：**
存储 kv 键值对

```javascript
try {
  await storage.setStorage('key1', 'val1')
  await storage.setStorage('key2', 'val2')
  console.log('set success')
} catch (e) {
  console.log(`set success failed ${JSON.stringify(err)}`)
}
```

### 3.3 getStorageKeys()

**参数**
*   无

**返回值**
*   异步方法：返回 key 列表

**用法：**
获取存储的所有 key 值

```javascript
let keys = await storage.getStorageKeys()
console.log(JSON.stringify(keys))
```

### 3.4 removeStorage()

**参数**
*   `key`：删除的 key 值

**返回值**
*   异步方法，返回0为成功，失败抛出异常

**用法：**
删除某个 key 值

```javascript
await storage.removeStorage('key1')
this.message = JSON.stringify(await storage.getStorageKeys())
```

### 3.5 clearStorage()

**参数**
*   无

**返回值**
*   异步方法，返回0为成功，失败抛出异常

**用法：**
清空存储的所有 kv 键值对

```javascript
await storage.clearStorage()
console.log(JSON.stringify(await storage.getStorageKeys()))
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 zip-压缩_解压</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # zip-压缩/解压

## 1. 概述
zip 模块可以操作zip格式的压缩文件，暂时仅支持解压操作。

## 2. 模块使用方式

```javascript
import zip from 'zip'
```

## 3. 方法

### 3.1 extractall()

**参数**
- `outPath`：解压目录

**返回值**
- 异步方法，返回0为成功，非0 为失败

**用法**
可以解压zip文件到指定目录，递归解压所有文件。

```javascript
const zfile = new zip.ZipFile('/tmp/test.zip')
let ret = await zfile.extractall('/tmp/test_extract')
console.log(`zip.extractall ret ${ret}`)
```
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary><strong>📁 系统服务</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 power - 电源管理</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # power - 电源管理

## 1. 概述
提供电源、电池、电量、开关机等信息和功能。

> **Tip：**
> 由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 模块使用方式

```javascript
import power from 'power'
```

## 3. 方法

### 3.1 getInfo()

**参数**
* 无

**返回值**
* `result` 格式如下

```javascript
{
  autoHibernate, // 闲置休眠开关 true: 启用；false:禁用
  hibernateTimeout, // 闲置时长 （单位：秒）
  batteryPercent, // 当前剩余电量
  isCharging // 电池状态   true:正在充电中；false: 未充电
}
```

**用法**
* 获取电源管理状态

```javascript
// 获取电源管理状态
power.getInfo().then((res) => {
  this.autoHibernate = res.autoHibernate;
  this.hibernateTimeout = res.hibernateTimeout;
  this.batteryPercent = res.batteryPercent;
  this.isCharging = res.isCharging;
});
```

### 3.2 setAutoHibernate(isOn)

**参数**
* `isOn` true-启用，false-禁用

**返回值**
* 无

**用法**
* 设置是否启用超时自动休眠

```javascript
// 设置系统闲置休眠
power.setAutoHibernate(true);
```

### 3.3 setHibernateTime(time)

**参数**
* `time` 闲置时长（单位：秒）

**返回值**
* 无

**用法**
* 设置自动休眠的闲置时间

```javascript
// 设置系统闲置30分钟后休眠
power.setHibernateTimeout(30 * 60); // 秒
```

### 3.4 shutdown()

**参数**
* 无

**返回值**
* 无

**用法**
* 立即关机

```javascript
// 立即关机
power.shutdown();
```

### 3.5 reboot()

**参数**
* 无

**返回值**
* 无

**用法**
* 立即重启

```javascript
// 立即重启
power.reboot();
```

## 4. 事件

### 4.1 batteryChange

**参数**
* 无

**返回值**
* `event` 格式如下

```javascript
{
  name, // change
  battery // 当前电量
}
```

**用法**
* 电池电量改变事件

```javascript
// 充电状态改变
power.on("change", (event) => {
  console.log("event.battery： " + event.battery);
})
```

### 4.2 charge

**参数**
* 无

**返回值**
* `event` 格式如下

```javascript
{
  name, // charge
  isCharging // true: 正在充电；false:未充电
}
```

**用法**
* 充电状态改变事件

```javascript
// 充电状态改变
power.on("charge", (event) => {
  console.log(event.isCharging ? "charging" : "on battery");
})
```

### 4.3 batteryLow

**参数**
* 无

**返回值**
* `event` 格式如下

```javascript
{
  name, // low
  battery // 当前电量
}
```

**用法**
* 电池电量低事件

```javascript
// 电量低
power.on("low", (event) => {
  console.log("notice battery low");
})
```

### 4.4 batteryEmergency

**参数**
* 无

**返回值**
* `event` 格式如下

```javascript
{
  name, // emergency
  battery // 当前电量
}
```

**用法**
* 紧急电量事件

```javascript
// 紧急电量，即将关机
power.on("emergency", (event) => {
  console.log("system shutting down");
})
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 screen-屏幕控制</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # screen-屏幕控制

## 1. 概述
屏幕控制封装，亮度、亮屏、灭屏、自动熄屏时间

**Tip：**
由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现

## 2. 模块使用方式

```javascript
import screen from 'screen'
```

## 3. 方法

### 3.1 getInfo()

**参数**
* 无

**返回值**
* result 格式

```javascript
{
    isOn // 屏幕显示状态 true: 亮屏；false:息屏
    isAutoBrightness //  自动调整屏幕亮度  true:启用；false: 禁用
    brightness // 当前屏幕亮度
    isAutoOff //自动息屏   true:启用；false: 禁用
    autoOffTimeout // 自动息屏时长 （单位：秒）
}
```

**用法**
* 获取屏幕配置和状态

```javascript
//  获取电源管理状态
screen.getInfo().then((res) => {
    this.isOn = res.isOn;
    this.isAutoBrightness = res.isAutoBrightness;
    this.brightness = res.brightness;
    this.isAutoOff = res.isAutoOff;
    this.autoOffTimeout = res.autoOffTimeout;
});
```

### 3.2 turnOn()

**参数**
* 无

**返回值**
* 无

**用法**
* 亮屏

```javascript
//亮屏
screen.turnOn();
```

### 3.3 turnOff()

**参数**
* 无

**返回值**
* 无

**用法**
* 熄屏

```javascript
//息屏
screen.turnOff();
```

### 3.4 setAutoBrightness(isAuto)

**参数**
* isAuto  true-启用，false-禁用

**返回值**
* 无

**用法**
* 设置是否启用自动亮度调节

```javascript
// 启用自动亮度调节
screen.setAutoBrightness(true);
```

### 3.5 setBrightness(percent)

**参数**
* percent  亮度

**返回值**
* 无

**用法**
* 设置亮度

```javascript
//设置亮度
screen.setBrightness(60);
```

### 3.6 setAutoOff(isAuto)

**参数**
* isAuto  true-启用，false-禁用

**返回值**
* 无

**用法**
* 设置是否启用闲置自动息屏

```javascript
//启用闲置自动息屏
screen.setAutoOff(true);
```

### 3.7 setAutoOffTimeout(time)

**参数**
* time  自动息屏市场（单位：秒）

**返回值**
* 无

**用法**
* 设置闲置自动灭屏时间，仅当启用自动灭屏时生效

```javascript
//设置系统闲置60秒后息屏
screen.setAutoOffTimeout(60);
```

## 4. 事件

### 4.1 status

**参数**
* 无

**返回值**
* event 格式

```javascript
{
    change //status
    isOn // 屏幕状态 true: 亮屏；false:息屏
}
```

**用法**
* 屏幕息屏或者亮屏会触发

```javascript
//充电状态改变
screen.on("status", (event) => {
    console.log("event.isOn： " + event.isOn);
})
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 system-系统通用</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # system-系统通用

## 1. 概述

模块: system, 用于系统操作

```javascript
// 导入system模块
import system from 'system'
```

## 2. 方法

### 2.1 reset()

接口功能：重置设备(恢复出厂设置)

入参：无

出参：无

示例：

```javascript
system.reset()
```

### 2.2 getVersion()

接口功能：获取系统版本号

入参：无

出参：

| 参数 | 类型 | 描述 |
| :--- | :--- | :--- |
| version | string | 系统版本 |

示例：

```javascript
let version = system.getVersion()
```

### 2.3 getSN()

接口功能：获取设备序列号

入参：无

出参：

| 参数 | 类型 | 描述 |
| :--- | :--- | :--- |
| sn | string | 序列号 |

示例：

```javascript
let sn = system.getSN()
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 system_kv - 系统kv存储</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # system_kv - 系统kv存储

## 1. 概述
全局的K-V持久化存储和读取封装。底层统一的存储介质，应用间可共享。

**Tip：**
由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 模块使用方式

```javascript
import skv from 'system_kv'
```

## 3. 方法

### 3.1 set(key, value)

**参数**
- `key` 存储项的名称
- `value` 存储项的值

**返回值**
- 无

**用法**
- 设置一个存储项

```javascript
//存储
skv.set('foo', 'test');
```

### 3.2 get(key [, defaultValue])

**参数**
- `key` 存储项的名称
- `defaultValue` 存储项的默认值，不传默认为 undefined

**返回值**
- `value`

**用法**
- 获取指定 key 的存储项的值。如果 key 不存在，则返回 defaultValue。

```javascript
//存储
skv.get('foo').then((res) => {
    this.value = res;
});
```

### 3.3 remove(key)

**参数**
- `key` 存储项的名称

**返回值**
- 无

**用法**
- 删除名称为 key 的存储项

```javascript
//删除
skv.remove('foo');
```

### 3.4 clear()

**参数**
- 无

**返回值**
- 无

**用法**
- 清除所有存储项

```javascript
//清除
skv.clear();
```

## 4. 事件
无
            ```
              </div>
            </details>
            <details>
              <summary>📄 wifi-无线网络</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # wifi-无线网络

## 1. 概述
wifi相关信息获取，WiFi的相关操作，比如连接，删除等功能。

**Tip：**
Linux系统上依赖wpa_suppliant 框架，目前框架默认不带，需要客户自己编译wpa_suppliant库。

## 2. 模块使用方式

```javascript
import wifi from 'wifi'
```

## 3. 方法

### 3.1 scan()
**参数**
* 无

**返回值**
* 无

**用法：**
发起扫描wifi操作，当有扫描结果时，通过事件"scan_result"通知。

```javascript
wifi.on('scan_result', (apList) => {
    console.log(apList)
})
wifi.scan()
```

### 3.2 scanResult()
**参数**
* 无

**返回值**
* apList

**用法：**
发起获取最近一次的扫描结果（如果有的话）。

```javascript
let apList = await wifi.scanResult()
```

### 3.3 addConfig()
**参数**
* ssid: ssid名称
* psk：密码

**返回值**
* 成功返回0，错误返回异常

**用法：**
增加ssid配置项。

```javascript
let ssid = 'ssid_name'
let psk = 'psk_value'
wifi.addConfig(ssid, psk)
```

### 3.4 removeConfig()
**参数**
* ssid: ssid名称

**返回值**
* 成功返回0，错误返回异常

**用法：**
删除ssid配置项。

```javascript
let ssid = 'ssid_name'
wifi.removeConfig(ssid)
```

### 3.5 listConfig()
**参数**
* 无

**返回值**
* configs：ssid配置列表 [{ssid: "ssid名称", "psk": "密码"}, ...]

**用法：**
列出 ssid 配置项列表。

```javascript
wifi.listConfig().then((configs) => {
    console.log(configs)
})
```

### 3.6 connect()
**参数**
* ssid: 连接的 ssid 名称

**返回值**
* 成功返回0，错误返回异常

**用法：**
发起某个 ssid 的连接请求。

```javascript
let ssid = 'ssid_name'
wifi.connect(ssid)
```

### 3.7 disconnect()
**参数**
* ssid: 连接的 ssid 名称

**返回值**
* 成功返回0，错误返回异常

**用法：**
发起某个 ssid 的断开请求。

```javascript
wifi.disconnect()
```

## 4. 事件

### 4.1 scan_result
**参数**
* 无

**返回值**
* [{status: "gotip", bssid: "ap的bssid", ssid: "ssid名称", rssi: 50, isEncrypt: true}, ...]

**字段含义：**
* status：ap状态，string类型，可选值：
    * "gotip"，连接成功
    * "disconnected"，连接失败
* bssid：ap的bssid值，string类型
* ssid：ssid名称，string类型
* rssi：信号强度，int类型
* authmode：加密类型，string类型，可选项：
    * OPEN
    * WEP
    * WPA_PSK
    * WPA2_PSK
    * WPA_WPA2_PSK
    * WPA2_ENTERPRISE
    * MAX

**用法：**
返回扫描 ap 列表。

```javascript
wifi.on('scan_result', (scanResult) => {
    console.log(scanResult)
})
```

### 4.2 disconnected
**参数**
* 无

**返回值**
* {reason: "connect_failed", bssid: "bssid值", ssid: "ssid名称"}

**字段含义：**
* reason：断连原因，string类型，可选值：
    * "connect_failed"，连接失败
    * "auth_failed"，秘钥失败
* bssid：ap的bssid值，string类型
* ssid：ssid名称，string类型

**用法：**
在wifi断连时触发。

```javascript
wifi.on('disconnected', (event) => {
    console.log(event)
})
```

### 4.3 connected
**参数**
* 无

**返回值**
* {reason: "", bssid: "bssid值", ssid: "ssid名称"}

**字段含义：**
* reason：控制字符串
* bssid：ap的bssid值，string类型
* ssid：ssid名称，string类型

**用法：**
在wifi连接成功时触发。

```javascript
wifi.on('connected', (event) => {
    console.log(event)
})
```
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary><strong>📁 网络请求</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 downloader-下载</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # downloader-下载

## 1. 概述
文件下载能力，提供大文件下载，断点续传等功能。

## 2. 模块使用方式

```javascript
import downloader from 'downloader'
```

## 3. 方法

### 3.1 download()

**参数**
- `params` 格式

```javascript
{
    taskId, // 必传 本次任务id，框架层带appId，保证全局唯一
    url, // 必传 下载地址
    outDir, // 必传 保存目录  建议目录为应用的data目录，后续会增加目录校验
    fileName, // 非必传  文件名称
    callbackDuration, // 非必传 进度回传周期(单位：毫秒)，若无，默认为3000ms, 不低于1000ms
    headers // 非必传 请求头附属信息
}
```

**返回值**
- 无

**用法**
- 文件下载

```javascript
//  文件下载
downloader.download({
    taskId: this.taskId,
    url: this.url,
    outDir: this.outDir,
    fileName: this.fileName,
    callbackDuration: this.callbackDuration,
    headers: {
        "User-Agent": "pan.baidu.com"
    }
});
```

### 3.2 cancel()

**参数**
- `taskId`

**返回值**
- 无

**用法**
- 取消下载，不保留缓存

```javascript
//  取消文件下载
downloader.cancel(taskId);
```

### 3.3 pause()

**参数**
- `taskId`

**返回值**
- 无

**用法**
- 暂停下载，保留缓存
- 增加说明：如需恢复本次下载，需要与第一次一样调用download方法

```javascript
//  暂停文件下载
downloader.pause(taskId);
```

## 4. 事件

### 4.1 begin

**参数**
- 无

**返回值**
- `taskId`

**用法**
- 开始下载

### 4.2 progress

**参数**
- 无

**返回值**
- `taskId`
- `args` 格式

```javascript
{
    loadSize: 已下载字节量,
    totalSize: 文件总字节量
}
```

**用法**
- 下载进度回调，根据设置的callbackDuration，若无设置，默认为3s

### 4.3 error

**参数**
- 无

**返回值**
- `taskId`
- `args` 格式

```javascript
{
    errorMsg: 错误信息,
    errorCode: 错误码
}
```

**用法**
- 下载失败

**错误码列表**

| 错误信息 (error msg) | error code | 含义 | 备注 |
| :--- | :--- | :--- | :--- |
| INVALID_URL | 101 | 下载地址为空 | |
| FILE_DIRECTORY_INVALID | 102 | 目标文件目录创建或打开失败 | |
| INVALID_DISK | 103 | 磁盘空间不足 | |
| INVALID_FILE | 104 | 文件打开异常 | |
| INITIALIZATION_INVALID | 105 | 下载初始化异常 | |
| TASK_CANCELED | 106 | 下载任务被取消 | |
| TASK_SUSPENDED | 107 | 下载任务被暂停 | |
| INVALID_PARAMS | 108 | 缺少必传参数 | |
| TASK_REPEAT | 109 | 重复下载 | |
| TASK_FAILED | 1- 80, 具体参考curl的错误码 | 下载失败 | curl下载失败错误信息 http://www.ttlsa.com/linux/curl-error-code-list/ |

### 4.4 finish

**参数**
- 无

**返回值**
- `taskId`

**用法**
- 下载成功

```javascript
downloader.on(this.taskId, (args) => {
    if (args.status == "begin") {
        this.outputMessage = 'download begin';
        console.log(this.outputMessage)
    } else if (args.status == "progress") {
        this.outputMessage = `download progress download ${args.loadSize}, total size ${args.totalSize}`;
        console.log(this.outputMessage)
    } else if (args.status == "error") {
        this.outputMessage = `download error ${args.errorMsg}, error code ${args.errorCode}`;
        console.log(this.outputMessage)
        return;
    } else if (args.status == "finish") {
        this.outputMessage = 'download finish';
        console.log(this.outputMessage)
        return;
    }
})
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 http_s-网络请求</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # http/s-网络请求

## 1. 概述
提供http请求和下载功能。

## 2. 模块使用方式

```javascript
import http from 'http';
```

## 3. 方法

### 3.1 request接口

**参数**

*   **Options:**
    *   `url`: 请求的url地址
    *   `method`: GET/POST/DELETE/PATCH/PUT
    *   `headers`: 自定义请求头 `{k1: v1, k2: v2}`
    *   `data`: 表单数据 `k1=v1&k2=v2`
    *   `timeout`：请求超时，单位ms，默认为5000ms

**返回值**

*   **HttpResponse**：`{status, headers, body}`

**用法**

发送http请求

```javascript
import http from 'http';

http.request({
    url: 'https://httpbin.org/get',
    method: 'GET',
    headers: {}
}).then((res) => {
    console.log(`http get then ${JSON.stringify(res)}`)
}).catch((err) => {
    console.log(err)
    console.log(`http get catch ${JSON.stringify(err)}`)
})

http.request({
    url: 'https://httpbin.org/post',
    method: 'POST',
    data: 'k1=v1&k2=v2',
    headers: {}
}).then((res) => {
    console.log(`http post then ${JSON.stringify(res)}`)
}).catch((err) => {
    console.log(err)
    console.log(`http post catch ${JSON.stringify(err)}`)
})
```

### 3.2 download接口

**参数**

*   **入参：**
    *   **Options:**
        *   `url`: 请求的url地址
        *   `method`: GET/POST/DELETE/PATCH/PUT
        *   `headers`: 自定义请求头 `{k1: v1, k2: v2}`
        *   `data`: 表单数据 `k1=v1&k2=v2`
        *   `outPath`: 下载路径
        *   `progress`: 进度回调 `(bytes, totalBytes) => {}`
        *   `timeout`：请求超时，单位ms，默认为5000ms

**返回值**

*   **HttpResponse**：`{status, headers}`

**用法**

通过http下载文件

```javascript
import http from 'http';

http.download({
    url: 'https://httpbin.org/get',
    method: 'GET',
    outPath: '/data/r.json',
    progress: (bytes, totalBytes) => {
        console.log(`http download progress ${bytes, totalBytes}`)
    }
}).then((res) => {
    console.log(`http download get then ${JSON.stringify(res)}`)
}).catch((err) => {
    console.log(err)
    console.log(`http download get catch ${JSON.stringify(err)}`)
})
```
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary><strong>📁 设备驱动</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 ADC</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # ADC-模拟数字转换器模块

## 1. 概述

模拟数字转换器即A/D转换器，或简称ADC，通常是指一个将模拟信号转变为数字信号的电子元件。通常的模数转换器是将一个输入电压信号转换为一个输出的数字信号。由于数字信号本身不具有实际意义，仅仅表示一个相对大小。故任何一个模数转换器都需要一个参考模拟量作为转换的标准，比较常见的参考标准为最大的可转换信号大小。而输出的数字量则表示输入信号相对于参考信号的大小。

> Tip：由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 模块使用方式

```javascript
import { adc } from "io";
```

## 3. 方法

### 3.1 open

**参数**

- Object对象

| 参数 | 类型 | 必选参数 | 说明 |
| :--- | :--- | :--- | :--- |
| port | String | 是 | adc的端口号码 |

**返回值**

- Object类型，ADC实例，为空代表创建失败。

**用法**

- 创建ADC实例

```javascript
// 创建ADC实例
adc1 = await adc.open({ port: "0" });
```

### 3.2 readValue

**参数**

- 无

**返回值**

- value: adc当前数据

**用法**

- 获取ADC数据

```javascript
// 创建ADC实例
adc1 = await adc.open({ port: "0" });
//获取ADC数据
var value = await adc1.readValue();
```

### 3.3 close

**参数**

- 无

**返回值**

- 无

**用法**

- 关闭ADC实例

```javascript
// 创建ADC实例
adc1 = await adc.open({ port: 0 });
//关闭ADC实例
await adc1.close();
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 GPIO</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # GPIO

## 1. 概述

GPIO(General-purpose input/output)，通用型之输入输出的简称，功能类似8051的P0—P3，其接脚可以供使用者由程控自由使用，PIN脚依现实考量可作为通用输入（GPI）或通用输出（GPO）或通用输入与输出（GPIO），如当clk generator, chip select等。

**Tip：**
由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 模块使用方式

```javascript
import { gpio } from "io";
```

## 3. 方法

### 3.1 open

**参数**

- Object类型

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| port | String | 是 | GPIO 端口号，比如"1000"等，代表 `/sys/class/gpio/gpio1000` |
| dir | String | 否 | GPIO 方向，默认为'in'<br>'in' 输入，'out' 输出，'high' 输出并置为高电平 , 'low' 输出并置为低电平 |
| active_low | int | 否 | GPIO 极性配置，默认为0<br>0：输出（输入）高电平为高电平，输出（输入）低电平为低电平<br>1：输出（输入）高电平为低电平，输出（输入）低电平为高电平 |
| intMode | String | 否 | GPIO 中断触发方式，默认为none<br>'rising'上升沿、'falling'下降沿、'both' 双边沿、'none' 不触发 |

**返回值**

- Object类型，GPIO实例，为空代表创建失败。

**用法**

- 创建GPIO实例

```javascript
// 创建gpio实例
this.led = await gpio.open({ port: "10", dir: "out", active_low: 0 });
```

### 3.2 writeValue

**参数**

- value 电平值 1：高电平，0：低电平

**返回值**

- 无

**用法**

- 向作为输出的GPIO写值，使其输出状态（电平）发生改变

```javascript
this.led = await gpio.open({ port: "10", dir: "out" });
// 写入gpio值
await led.writeValue(1); // 输出高电平
await led.writeValue(0); // 输出低电平
```

### 3.3 readValue

**参数**

- 无

**返回值**

- value 电平值 1：高电平，0：低电平

**用法**

- 读取输入模式下的gpio值

```javascript
this.led = await gpio.open({ port: "10", dir: "in" });
// 读取gpio值
var value = await led.readValue();
```

### 3.4 toggle

**参数**

- 无

**返回值**

- 无

**用法**

- 切换GPIO的电平，当前GPIO电平为低电平时设置为高电平，当前GPIO电平为高电平时设置为低电平。

```javascript
this.led = await gpio.open({ port: "10", dir: "out" });
// 翻转电平
await led.toggle();
```

### 3.5 close

**参数**

- 无

**返回值**

- 无

**用法**

- 关闭GPIO实例

```javascript
this.led = await gpio.open({ port: "10", dir: "irq", intMode: "falling" });
// 关闭gpio实例
await led.close();
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 PWM</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # PWM

## 1. 概述

脉冲宽度调制是一种模拟控制方式，根据相应载荷的变化来调制晶体管基极或MOS管栅极的偏置，来实现晶体管或MOS管导通时间的改变，从而实现开关稳压电源输出的改变。这种方式能使电源的输出电压在工作条件变化时保持恒定，是利用微处理器的数字信号对模拟电路进行控制的一种非常有效的技术。脉冲宽度调制是利用微处理器的数字输出来对模拟电路进行控制的一种非常有效的技术，广泛应用在从测量、通信到功率控制与变换的许多领域中。

> **Tip：**
> 由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 模块使用方式

```javascript
import { pwm } from "io";
```

## 3. 方法

### 3.1 open

**参数**
- `object` 类型

| 参数 | 类型 | 必选参数 | 说明 |
| :--- | :--- | :--- | :--- |
| port | String | 是 | pwm的端口号码 |
| duty | Number | 否 | 默认为50。设置PWM占空比，范围在0 ~ 100，单位是百分比 |
| freq | Number | 否 | 默认为1000。设置PWM的频率，单位是HZ |

**返回值**
- `Object` 类型，PWM实例，为空代表创建失败。

**用法**
- 创建PWM实例

```javascript
// 创建PWM实例
this.pwm1 = await pwm.open({ port: "3", duty: 60, freq: 500 });
```

### 3.2 set

**参数**
- `object` 类型

| 参数 | 类型 | 必选参数 | 说明 |
| :--- | :--- | :--- | :--- |
| duty | Number | 否 | 设置PWM占空比，范围在0 ~ 100，单位是百分比 |
| freq | Number | 否 | 设置PWM的频率，单位是HZ |

**返回值**
- 无，出错会直接报错。

**用法**
- 设置PWM参数。

```javascript
// 创建PWM实例
pwm1 = await pwm.open({ port: "3" });
await pwm1.set({ duty: 50, freq: 2000 });
```

### 3.3 get

**参数**
- 无

**返回值**
- `Object` 类型

| 参数 | 类型 | 说明 |
| :--- | :--- | :--- |
| duty | Number | 设置PWM占空比，范围在0 ~ 100，单位是百分比 |
| freq | Number | 设置PWM的频率，单位是HZ |

**用法**
- 获取PWM参数

```javascript
// 创建PWM实例
pwm1 = await pwm.open({ port: 3 });
this.res = await pwm1.get();
this.duty = this.res.duty;
this.freq = this.res.freq;
```

### 3.4 enable

**参数**
- `enable` // true:使能 false:失能

**返回值**
- 无

**用法**
- 使能/失能PWM实例

```javascript
// 创建PWM实例
pwm1 = await pwm.open({ port: 3 });
await pwm1.set({ duty: 50, freq: 2000 });
// 失能PWM
await pwm1.enable(false);
```

### 3.5 close

**参数**
- 无

**返回值**
- 无

**用法**
- 关闭PWM实例

```javascript
// 创建PWM实例
pwm1 = await pwm.open({ id: 3 });
await pwm1.set({ duty: 50, freq: 2000 });
// 关闭PWM实例
await pwm1.close();
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 UART</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # UART

## 1. 概述

通用异步收发器是一种通用串行数据总线，用于异步通信。该总线双向通信，可以实现全双工传输和接收。在嵌入式设计中，UART用来与PC进行通信，包括与监控调试器和其它器件，如EEPROM通信。

> **Tip：**
> 由于每个芯片都不一样，且无统一开源库，目前只提供JSAPI接口，框架默认不带且无实现。

## 2. 方法

### open(Object options)

打开串口，创建 UART 实例。

#### 入口参数

| 属性 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| port | Int | 是 | 要打开的串口的设备路径 |
| dataWidth | Int | 是 | 串口数据宽度，单位“位” |
| baudRate | Int | 是 | 串口波特率，默认115200 |
| stopBits | Int | 是 | 串口停止位个数<br>有效参数：1，2 |
| parity | String | 是 | 校验位<br>支持参数：“none”,"odd","even" |
| flowControl | String | 否 | 支持参数：<br>“disable”，“cts”，“rts”，“rtscts” |
| mode | String | 否 | poll，配置为轮询主动读模式<br>配置为空，配置为中断监听读取模式 |

#### 返回参数
串口实例

#### 使用示例
```javascript
import { uart } from 'io'
let com1 = await uart.open({
    port: "/dev/tty.usbserial-14140",
    dataWidth: 8,
    baudRate: 115200,
    stopBits: 1,
    flowControl: "disable",
    parity: "none"
})
```

### write(Uint8Array|String data)

向串口发送数据，该函数为阻塞函数，串口发送完成后才会返回。

#### 入口参数

| 参数 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| data | Uint8Array/String | 是 | 需要发送到串口的数据，可以是数组或字符串、ArrayBuffer |

#### 返回参数
无

#### 使用示例
```javascript
import { uart } from 'io'
let com1 = await uart.open({
    port: "/dev/tty.usbserial-14140",
    dataWidth: 8,
    baudRate: 115200,
    stopBits: 1,
    flowControl: "disable",
    parity: "none"
})
var buffer = [0x01, 0x02, 0x03, 0x04]
let res
res = await com1.write("hello world")
res = await com1.write(buffer)
```

### read(Number bytes, Number timeout)

当“mode”参数为“poll”时，用阻塞的方式读取串口数据，读到指定量的数据或超时后退出。

#### 入口参数

| 参数 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| bytes | Number | 是 | 单次读取数据的最大长度，超出长度范围的数据在下次读取时返回。 |
| timeout | Number | 是 | 轮询读超时等待时间 毫秒 |

#### 返回参数

| 类型 | 描述 |
| :--- | :--- |
| data | ArrayBuffer 从串口读取的数据<br>读取到的数据长度，如果没有读取到数据，返回0 |

#### 使用示例
```javascript
import { uart } from 'io'
let com1 = await uart.open({
    port: "/dev/tty.usbserial-14140",
    dataWidth: 8,
    baudRate: 115200,
    stopBits: 1,
    flowControl: "disable",
    parity: "none",
    mode: "poll"
})
/* ArrayBuffer 转字符串 */
function ArrayBufferToString(buf) {
    return String.fromCharCode.apply(null, new Uint8Array(buf))
}
// 读取10个字节，超时500ms
let buffer = await com1.read(buffer, 10, 500)
console.log(ArrayBufferToString(buffer))
```

### on(String event, Function cb)

串口事件回调，当串口发生如收到数据时（“mode”参数不为“poll”时）将触发回调。

#### 入口参数

| 参数 | 类型 | 必填 | 描述 |
| :--- | :--- | :--- | :--- |
| event | String | 是 | 目前支持的事件：<br>"data",在非"poll"模式下收到数据 |
| cb | Function | 是 | 回调函数 function(onData) {}<br>onData数据格式为arraybuffer。 |

#### 返回参数
无

#### 使用示例
```javascript
import { uart } from 'io'
let com1 = await uart.open({
    port: "/dev/tty.usbserial-14140",
    dataWidth: 8,
    baudRate: 115200,
    stopBits: 1,
    flowControl: "disable",
    parity: "none"
})
/* ArrayBuffer 转字符串 */
function ArrayBufferToString(buf) {
    return String.fromCharCode.apply(null, new Uint8Array(buf))
}
/* ArrayBuffer 转 Uint8Array */
function ArrayBufferToUint8Array(buf) {
    return Array.prototype.slice.call(new Uint8Array(buf))
}
com1.on("data", function(onData) {
    /* 打印出串口接收到的数据，数据类型为 ArrayBuffer，先转为字符串后再打印 */
    console.log("uart on: " + ArrayBufferToString(onData))
    /* 串口把接收到的数据直接回发出去 */
    com1.write(onData)
})
```

### close()

关闭串口，销毁实例。

#### 入口参数
无

#### 返回参数
无
            ```
              </div>
            </details>
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary>📄 模拟器混入 mock jsapi 代码</summary>
      <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
    ```markdown
    # 模拟器 mock JSAPI

有一些设备 JSAPI，比如 GPIO 需要在设备才能验证。
如下介绍如何使用模拟器 mock 这些 JSAPI。

有两种混入方法：
1.  simulator 方式启动模拟器默认混入 mock jsapi 代码
2.  build 命令时通过 `--mock` 选项强制混入 mock jsapi 代码

## 编写 mock 逻辑

*   在vue工程根目录下的 `api-mock` 文件夹中加入 JSAPI mock 文件，比如 `qjs-dbus.js`

```javascript
// qjs-dbus.js
export default {
  getBus(name) {
    console.log(`invoking mock JSAPI of qjs-dbus name: ${name}`)
    return {}
  }
}
```

## 在 JS 中使用

与真实设备 JSAPI 使用一样

```javascript
import dbus from 'qjs-dbus'
dbus.getBus('wlan0') // invoking mock JSAPI of qjs-dbus name: wlan0
```

## simulator 方式运行

### 配置 simulator

通过 `aiot-cli s .` 命令运行，s 是 simulator 命令的缩写

```javascript
// 修改 package.json 配置模拟器路径
"simulator": {
  "path": "/Users/netsec/works/haasui/miniapp_falcon/cmake-build-debug/usr/bin",
  "page": ""
},
```

## build --mock 方式

通过 `aiot-cli build --mock` 来支持混入 api-mock 代码，任意方式加载运行 app
    ```
      </div>
    </details>
  </div>
</details>
<details>
  <summary><strong>📁 应用开发</strong></summary>
  <div style="margin-left: 20px;">
    <details>
      <summary><strong>📁 CSS样式</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary><strong>📁 通用样式</strong></summary>
          <div style="margin-left: 20px;">
            <details>
              <summary>📄 Flexbox</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # Flexbox

## 简介

布局模型基于 CSS **Flexbox**，以便所有页面元素的排版能够一致可预测，同时页面布局能适应各种设备或者屏幕尺寸。Flexbox 包含 flex 容器和 flex 成员项。如果一个元素可以容纳其他元素，那么它就成为 flex 容器。

> **文档中未说明的 flexbox 属性均不支持**：如 `order`、`flex-flow` 等。

## Flex 容器

Flexbox 是默认且唯一的布局模型，所以你不需要手动为元素添加 `display: flex;` 属性。

### direction

`direction` 决定了文字方向和 **Flex容器** 的基线方向。默认值为 `ltr`。

*   `ltr`: 文字和其他元素从左到右排布
*   `rtl`: 文字和其他元素从右到左排布。

> **TIP**
> 尽管 `direction` 不是 Flexbox模型的一部分，但却对 Flexbox 模型有着影响。

### flex-direction

`flex-direction` 定义了 flex 容器中 flex 成员项的排列方向，默认值为 `column`

*   `column`：从上到下排列。
*   `column-reverse`: 从下到上排布
*   `row`：如果存在 `direction:ltr`，则从左到右排布；如果存在 `direction:rtl`，则从右到左排布。
*   `row-reverse`: 排布方向与 `flex-direction:row` 相反

### flex-wrap

`flex-wrap` 属性决定了 **Flex成员项** 在一行还是多行分布，默认值为 `nowrap`

*   `nowrap`: **Flex成员项** 在一行排布，排布的开始位置由 `direction` 指定。
*   `wrap`：**Flex成员项** 在多行排布，排布的开始位置由 `direction` 指定
*   `wrap-reverse`: 行为类似于 `wrap`，排布方向与其相反。

### justify-content

定义了 flex 容器中 flex 成员项在主轴方向上如何排列以处理空白部分。可选值为 `flex-start` | `flex-end` | `center` | `space-between`，默认值为 `flex-start`。

*   `flex-start`：是默认值，所有的 flex 成员项都排列在容器的前部；
*   `flex-end`：则意味着成员项排列在容器的后部；
*   `center`：即中间对齐，成员项排列在容器中间、两边留白；
*   `space-between`：表示两端对齐，空白均匀地填充到 flex 成员项之间。

### align-items

定义了 flex 容器中 flex 成员项在纵轴方向上如何排列以处理空白部分。可选值为 `stretch` | `flex-start` | `center` | `flex-end`，默认值为 `stretch`。

*   `stretch` 是默认值，即拉伸高度至 flex 容器的大小；
*   `flex-start` 则是上对齐，所有的成员项排列在容器顶部；
*   `flex-end` 是下对齐，所有的成员项排列在容器底部；
*   `center` 是中间对齐，所有成员项都垂直地居中显示。

## Flex 成员项

### flex

`flex` 属性定义了 flex 成员项可以占用容器中剩余空间的大小。如果所有的成员项设置相同的值 `flex: 1`，它们将平均分配剩余空间。如果一个成员项设置的值为 `flex: 2`，其它的成员项设置的值为 `flex: 1`，那么这个成员项所占用的剩余空间是其它成员项的 2 倍。Flex 成员项暂不支持 `flex-shrink` 和 `flex-basis` 属性。

*   `flex {number}`：值为 number 类型。

> **该属性不支持 `flex: <flex-grow> | <flex-shrink> | <'flex-basis'>` 的简写。**

## 定位

支持 `position` 定位，用法与 CSS position 类似。为元素设置 `position` 后，可通过 `top`、`right`、`bottom`、`left` 四个属性设置元素坐标。

*   `position {string}`：设置定位类型。可选值为 `relative` | `absolute` | `fixed` | `sticky`，默认值为 `relative`。
    *   `relative` 是默认值，指的是相对定位；
    *   `absolute` 是绝对定位，以元素的容器作为参考系；
    *   `fixed` 保证元素在页面窗口中的对应位置显示；
    *   `sticky` 指的是仅当元素滚动到页面之外时，元素会固定在页面窗口的顶部。
*   `top {number}`：距离上方的偏移量，默认为 0。
*   `bottom {number}`：距离下方的偏移量，默认为 0。
*   `left {number}`：距离左方的偏移量，默认为 0。
*   `right {number}`：距离右方的偏移量，默认为 0。
            ```
              </div>
            </details>
            <details>
              <summary>📄 Transform</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # Transform

## 简介

除了 `perspective` 和 `transform-origin`，`transition` 支持了 `transform` 的全部能力。其中 `transform` 的 `rotate` 和 `rotatez` 等效。

目前支持的 transform 声明格式:

*   `translateX({<length/percentage>})`：X 轴方向平移，支持长度单位或百分比。
*   `translateY({<length/percentage>})`：Y 轴方向平移，支持长度单位或百分比。
*   `translate({<length/percentage>} {<length/percentage>})`：X 轴和 Y 轴方向同时平移，`translateX` + `translateY` 简写。
*   `scaleX(<number>)`：X 轴方向缩放，值为数值，表示缩放比例，**不支持百分比**。
*   `scaleY(<number>)`：Y 轴方向缩放，值为数值，表示缩放比例，**不支持百分比**。
*   `scale(<number>)`：X 轴和 Y 轴方向同时缩放，`scaleX` + `scaleY` 简写。
*   `rotate(<angle/degree>)`：将元素围绕一个定点（由 `transform-origin` 属性指定）旋转而不变形的转换。指定的角度定义了旋转的量度。若角度为正，则顺时针方向旋转，否则逆时针方向旋转。
*   `rotateX(<angle/degree>)`：X 轴方向的旋转。
*   `rotateY(<angle/degree>)`：Y 轴方向的旋转。
*   `rotateZ(<angle/degree>)`：Z 轴方向的旋转。
*   `transform-origin {length/percentage/关键字(top/left/right/bottom)}:`：设置一个元素变形的原点，**仅支持 2D 坐标**。

## 示例

```html
<template>
  <div class="wrapper">
    <div class="transform">
      <text class="title">Transformed element</text>
    </div>
  </div>
</template>
<style>
  .transform {
    align-items: center;
    transform: translate(150px, 200px) rotate(20deg);
    transform-origin: 0 -250px;
    border-color: red;
    border-width: 2px;
  }
  .title {
    font-size: 48px;
  }
</style>
```

在 native 端，给组件设置 `transform` 变换后，如果需要恢复原效果，不能直接删除对应的 `transform` 属性，而需要重新设置一个 `transform` 将元素变换恢复。可对比以下两个示例(待提供)。
            ```
              </div>
            </details>
            <details>
              <summary>📄 Transition</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # Transition

## 简介

现在您可以在 CSS 中使用 `transition` 属性来提升您应用的交互性与视觉感受。`transition` 中包括布局动画，即 LayoutAnimation，现在布局产生变化的同时也能使用 `transition` 带来的流畅动画。`transition` 允许 CSS 的属性值在一定的时间区间内平滑地过渡。

## 参数

### transition-property
设置过渡动画的属性名，设置不同样式 `transition` 效果的键值对，默认值为空，表示不执行任何过渡效果，下表列出了所有合法的参数属性：

| 参数名 | 描述 |
| :--- | :--- |
| width | 设置组件的宽度参与过渡动画 |
| height | 设置组件的高度参与过渡动画 |
| top | 设置组件的顶部距离参与过渡动画 |
| bottom | 设置组件的底部距离参与过渡动画 |
| left | 设置组件的左侧距离参与过渡动画 |
| right | 设置组件的右侧距离参与过渡动画 |
| background-color | 设置组件的背景颜色参与过渡动画 |
| opacity | 设置组件的不透明度参与过渡动画 |
| transform | 设置组件的变换类型参与过渡动画 |

### transition-duration
指定过渡的持续时间 (单位是毫秒)，默认值是 `0`，表示没有动画效果。

### transition-delay
指定请求过渡操作到执行过渡之间的时间间隔 (单位是毫秒或者秒)，默认值是 `0`，表示没有延迟，在请求后立即执行过渡。

### transition-timing-function
描述过渡执行的速度曲线，用于使过渡更为平滑。默认值是 `ease`。下表列出了所有合法的属性：

| 属性名 | 描述 |
| :--- | :--- |
| ease | transition 过渡逐渐变慢的过渡效果 |
| ease-in | transition 过渡慢速开始，然后变快的过渡效果 |
| ease-out | transition 过渡快速开始，然后变慢的过渡效果 |
| ease-in-out | transition 过渡慢速开始，然后变快，然后慢速结束的过渡效果 |
| linear | transition 过渡以匀速变化 |
| cubic-bezier(x1, y1, x2, y2) | 使用三阶贝塞尔函数中自定义 transition 变化过程，函数的参数值必须处于 0 到 1 之间。更多关于三次贝塞尔的信息请参阅 `cubic-bezier` 和 `Bézier curve`。 |

## 示例

```css
<style scoped>
.panel {
    margin: 10px;
    top:10px;
    align-items: center;
    justify-content: center;
    border: solid;
    border-radius: 10px;
    transition-property: width, height, background-color;
    transition-duration: 0.3s;
    transition-delay: 0s;
    transition-timing-function: cubic-bezier(0.25, 0.1, 0.25, 1.0);
}
</style>
```
            ```
              </div>
            </details>
            <details>
              <summary>📄 其他基本样式</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # 其他基本样式

## Opacity

`opacity {number}`：取值范围为 [0, 1] 区间。默认值是 1，即完全不透明；0 是完全透明；0.5 是 50% 的透明度。

## background-color

`background-color {color}`：设定元素的背景色，可选值为色值，支持：
- RGB（`rgb(255, 0, 0)`）
- RGBA（`rgba(255, 0, 0, 0.5)`）
- 十六进制（`#ff0000`）
- 精简写法的十六进制（`#f00`）
- 色值关键字（`red`）

默认值是 `transparent`。
            ```
              </div>
            </details>
            <details>
              <summary>📄 盒模型</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # 盒模型

页面布局的盒模型基于 CSS 盒模型，每个组件元素都可视作一个盒子。我们一般在讨论设计或布局时，会提到「盒模型」这个概念。

盒模型描述了一个元素所占用的空间。每一个盒子有四条边界：外边距边界 margin edge, 边框边界 border edge, 内边距边界 padding edge 与内容边界 content edge。这四层边界，形成一层层的盒子包裹起来，这就是盒模型大体上的含义。

盒模型的 `box-sizing` 默认为 `border-box`，即盒子的宽高包含内容、内边距和边框的宽度，不包含外边距的宽度。

默认是 `overflow:hidden`。

下面的例子显示了盒模型的基本用法：

```html
<template>
  <div>
    <image src="..." style="width: 400; height: 200; margin-left: 20;"></image>
  </div>
</template>
```

## 宽度

`width {length}`：默认值 0

## 高度

`height {length}`：默认值 0

## 内边距

`padding {length}`：内边距，内容和边框之间的距离，默认值 0。与标准 CSS 类似，`padding` 支持简写，也可分解为以下四个：

* `padding {length}`: 上、下、左、右四边内边距，默认值 0
* `padding-left {length}`：左内边距，默认值 0
* `padding-right {length}`：右内边距，默认值 0
* `padding-top {length}`：上内边距，默认值 0
* `padding-bottom {length}`：下内边距，默认值 0

## 边框

### border-style

`border-style` 设定边框样式，如果四个方向的边框样式不同，可分别设置：

* `border-style {string}`
* `border-left-style {string}`：可选值为 `solid` | `dashed` | `dotted`，默认值 `solid`
* `border-top-style {string}`：可选值为 `solid` | `dashed` | `dotted`，默认值 `solid`
* `border-right-style {string}`：可选值为 `solid` | `dashed` | `dotted`，默认值 `solid`
* `border-bottom-style {string}`：可选值为 `solid` | `dashed` | `dotted`，默认值 `solid`

支持的值如下：

* `solid`：实线边框，默认值 `solid`
* `dashed`：方形虚线边框
* `dotted`：圆点虚线边框

### border-width

`border-width`：设定边框宽度，非负值, 默认值 0，如果四个方向的边框宽度不同，可分别设置：

* `border-width {length}`：非负值, 默认值 0
* `border-left-width {length}`：非负值, 默认值 0
* `border-top-width {length}`：非负值, 默认值 0
* `border-right-width {length}`：非负值, 默认值 0
* `border-bottom-width {length}`：非负值, 默认值 0

### border-color

`border-color`：设定边框颜色，默认值 `#000000`，如果四个方向的边框颜色不同，可分别设置：

* `border-color {color}`：默认值 `#000000`
* `border-left-color {color}`：默认值 `#000000`
* `border-top-color {color}`：默认值 `#000000`
* `border-right-color {color}`：默认值 `#000000`
* `border-bottom-color {color}`：默认值 `#000000`

### border-radius

`border-radius`：设置边框的圆角，默认值 0，如果四个方向的圆角弧度不同，可分别设置：

* `border-radius {length}`: 非负值, 默认值 0
* `border-bottom-left-radius {length}`：非负值, 默认值 0
* `border-bottom-right-radius {length}`：非负值, 默认值 0
* `border-top-left-radius {length}`：非负值, 默认值 0
* `border-top-right-radius {length}`：非负值, 默认值 0

> **WARNING**
> `border-radius` 和 `border-width` 定义了圆心角为90度的椭圆弧的长轴和半长轴的大小。如果邻接两边 `border-radius` (或 `border-width`) 不一致，页面绘制的边框曲线可能不够平滑。

## 外边距

`margin {length}`：外边距，元素和元素之间的空白距离，默认值 0。与标准 CSS 类似，`margin` 支持简写，也可分解为四边：

* `margin {length}`: 上、下、左、右四边外边距，默认值 0
* `margin-left {length}`：左外边距，默认值 0
* `margin-right {length}`：右外边距，默认值 0
* `margin-top {length}`：上外边距，默认值 0
* `margin-bottom {length}`：下外边距，默认值 0
            ```
              </div>
            </details>
            <details>
              <summary>📄 线性渐变</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # 线性渐变

## 简介
支持线性渐变背景，具体介绍可参考 [CSS 渐变介绍](https://developer.mozilla.org/zh-CN/docs/Web/CSS/CSS_Images/Using_CSS_gradients)。

所有组件均支持线性渐变。

## 使用
你可以通过 `background-image` 属性创建线性渐变。

```css
background-image: linear-gradient(to top, #a80077, #66ff00);
```

目前暂不支持 `radial-gradient`（径向渐变）。

目前只支持两种颜色的渐变，渐变方向如下：

* `to right`：从左向右渐变
* `to left`：从右向左渐变
* `to bottom`：从上到下渐变
* `to top`：从下到上渐变
* `to bottom right`：从左上角到右下角
* `to top left`：从右下角到左上角

## 警告

* `background-image` 优先级高于 `background-color`，这意味着同时设置 `background-image` 和 `background-color`，`background-color` 被覆盖。
* `background` 不支持简写。
            ```
              </div>
            </details>
            <details>
              <summary>📄 阴影(box-shadow)</summary>
              <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
            ```markdown
            # 阴影(box-shadow)

## 简介

`box-shadow` 属性用于设置元素阴影。

## 参数

*   **`<offset-x>`**
    *   单位：`px`
    *   说明：设置阴影的水平偏移量。如果是负值，则阴影位于元素左边。

*   **`<offset-y>`**
    *   单位：`px`
    *   说明：设置阴影的垂直偏移量。如果是负值，则阴影位于元素上面。
    *   **注意**：如果 `<offset-x>` 和 `<offset-y>` 两者都是 0，那么阴影位于元素正后方。这时如果设置了 `<blur-radius>` 或 `<spread-radius>`，则会产生模糊效果。

*   **`<blur-radius>`**
    *   单位：`px`
    *   说明：设置模糊半径。值越大，模糊面积越大，阴影就越大、越淡。
    *   **注意**：不能为负值。默认为 0，此时阴影边缘锐利。

*   **`<color>`**
    *   说明：设置阴影的颜色。可参考 CSS 颜色单位。
            ```
              </div>
            </details>
          </div>
        </details>
        <details>
          <summary>📄 CSS使用注意点</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # CSS使用注意点

## 伪类支持情况

目前只支持 `:active` 伪类，其他不支持。
        ```
          </div>
        </details>
        <details>
          <summary>📄 CSS切换-多主题切换</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # CSS切换-多主题切换

## 【通用-主题】如何配置一套主题色

### 背景
UED 出设计稿时通常会有设计规范，通过提炼我们可以归纳为：色彩、字体、字号、边距、间距、圆角、线条。
通过调整并利用好这几组变量即可使产品风格产生很多变化。
可修改变量可参考附录《主题系统样式表》章节

如下例我们使用 ui-demo 作为基础进行演示。
虽然通篇文章在讲主题切换（样式），但是也可以用来控制屏幕尺寸等css variable，例如：通过编译期设定 app.json 的选项 options.style.themes，控制哪个屏幕尺寸 css 被编译到小程序中。

### 效果
可以看到主题色、间距等已经发生变化

原主题                                          ----->                             主题1

### 实现

#### falcon-ui 主题机制简介
由于 falcon-ui 主题机制在 app.json 中提供了指定 theme-custom 名称，并约定路径进行加载
约定的路径格式为：`src/styles/<theme-custom>/`
其中的组织形式与 falcon-ui 中内置的主题样式组织形式一致，以 theme-dark 为例有如下主要文件：

```
THEME-DARK
├── COMPONENT
├── GLOBALS
│   └── THEME.VARIABLES.LESS
└── THEME.CONFIG.JS
```

*   component 中是对组件的样式定制
*   globals/theme.variables.less 是全局变量覆盖
    *   全局变量在 `falcon-ui/src/styles/globals/theme.variables.less`
*   theme.config.js 是对组件的默认 props 做覆盖，比如 radio size 等

#### 主题色定制举例
主要目标是覆盖 `theme.variables.less` 定义
我们定义两套 theme 分别为 custom-theme1 、custom-theme2

*   **主题目录结构创建**
    利用 falcon-ui 主题加载机制，我们创建如下目录结构，并在 app.json 中使用 customTheme 变量进行切换。

    ```
    SRC/
    ├── STYLES/
    │   ├── THEME-CUSTOM1/
    │   │   ├── COMPONENT/
    │   │   └── GLOBALS/
    │   │       └── THEME.VARIABLES.LESS
    │   ├── THEME-CUSTOM2/
    │   │   └── GLOBALS/
    │   │       └── THEME.VARIABLES.LESS
    │   ├── BASE.LESS
    │   ├── MIXIN.LESS
    │   └── VAR.LESS
    ├── APP.JS
    └── APP.JSON
    ```

*   **app.json 主题切换**

    ```json
    {
        // ...
        "options": {
            "style": {
                "themeCustom": "theme-custom2"
            }
        }
    }
    // 或者
    {
        // ...
        "options": {
            "style": {
                "themeCustom": "theme-custom1"
            }
        }
    }
    ```

*   **主题样式内容**
    `styles/theme-custom1/globals/theme.variables.less`

    ```less
    @primary: #3960CC;
    @white: #FFFFFF;
    @black: #000000;
    @background-color: #0C121B;
    @card-background-color: #232930;
    @secondary: #353D48;
    @btn-background-color: #4B5462;
    @light-green: #7CFF00;
    @green: #2D901E;
    @blue: #376FDB;
    @yellow: #FF9500;
    @purple: #893FCD;
    @cyan: #209AAD;
    @red: #EF4141;
    @font-size-title-large: 36px;
    @font-size-title-medium: 32px;
    @font-size-title-small: 28px;
    @font-weight-title-large: @font-weight-medium;
    @font-weight-title-medium: @font-weight-medium;
    @font-weight-title-small: @font-weight-medium;
    @font-size-content-large: 24px;
    @font-size-content-medium: 20px;
    @font-size-content-small: 18px;
    @font-weight-content-large: @font-weight-normal;
    @font-weight-content-medium: @font-weight-normal;
    @font-weight-content-small: @font-weight-normal;
    @space-very-compact: 4px;
    @space-compact: 8px;
    @space-normal: 12px;
    @space-loose: 16px;
    @gap-very-compact: 4px;
    @gap-compact: 8px;
    @gap-normal: 12px;
    @gap-loose: 16px;
    @radius-small: 2px;
    @radius-medium: 4px;
    @radius-normal: 6px;
    @radius-large: 8px;
    @border-small: 1px;
    @border-medium: 2px;
    @border-normal: 3px;
    @border-large: 4px;
    ```

    `styles/theme-custom2/global/theme.variables.less`

    ```less
    @primary: #0ECC9B;
    @white: #FFFFFF;
    @black: #000000;
    @background-color: #1D2336;
    @card-background-color: #3A455F;
    @secondary: #4D5571;
    @btn-background-color: #737F9C;
    @light-green: #7CFF00;
    @green: #2D901E;
    @blue: #376FDB;
    @yellow: #FF9500;
    @purple: #893FCD;
    @cyan: #209AAD;
    @red: #EF4141;
    @font-size-title-large: 36px;
    @font-size-title-medium: 32px;
    @font-size-title-small: 28px;
    @font-weight-title-large: @font-weight-medium;
    @font-weight-title-medium: @font-weight-medium;
    @font-weight-title-small: @font-weight-medium;
    @font-size-content-large: 24px;
    @font-size-content-medium: 20px;
    @font-size-content-small: 18px;
    @font-weight-content-large: @font-weight-normal;
    @font-weight-content-medium: @font-weight-normal;
    @font-weight-content-small: @font-weight-normal;
    @space-very-compact: 8px;
    @space-compact: 16px;
    @space-normal: 20px;
    @space-loose: 24px;
    @gap-very-compact: 8px;
    @gap-compact: 16px;
    @gap-normal: 20px;
    @gap-loose: 24px;
    @radius-small: 16px;
    @radius-medium: 24px;
    @radius-normal: 32px;
    @radius-large: 40px;
    @border-small: 1px;
    @border-medium: 2px;
    @border-normal: 3px;
    @border-large: 4px;
    ```

### 效果
可以看到主题色、间距等已经发生变化

### 主题变量设计&应用原理
如下列出主题变量中应用的场景，可以灵活使用。

*   **色彩**
    *   主题色，比如 primary button 即主题色；
    *   背景色，比如页面背景色；
    *   卡片背景色，顾名思义，卡片元素的背景色，视觉上会与背景色区分；
    *   辅助色，一些组件设计时主色相对的辅助色， 比如按钮有“确定”“取消”，其“确定”一般为主色，另外为辅助色；
    *   按钮色，按钮默认（default）时的颜色；
    *   功能色_*，其他色相，配合使用颜色成体系。
*   **字体、字号、字重**
    *   标题：大中小标题
    *   正文：正文、二级正文、辅助文
*   **边距**
    *   通常为 padding，即内容到容器边的距离
*   **间距**
    *   通常为 margin，即两个元素之间的距离
*   **圆角**
    *   通常为 border-radius，即容器的边角圆弧
*   **线条**
    *   通常为 border-width，即容器边的粗细

## 【通用-主题】theme切换系统机制

### 编译配置
在 app.json 中配置theme后，通过 cli 编译主题样式

```javascript
// app.json
{
    "options": {
        "style": {
            "themes": ["theme-dark", "theme-custom1", "theme-custom2"]
        }
    }
}
```

### 系统全局配置

```javascript
// resources/env.json
{
    "theme": "theme-custom2"
}
```

### 系统动态改变

```javascript
methods: {
    onChangeTheme() {
        const themes = ["theme-dark", "theme-custom1", "theme-custom2"]
        this.index = this.index || 0
        this.index = this.index++ % themes.length
        const theme = themes[this.index]
        $falcon.env.custom.$set('theme', theme)
    }
}
```

## 【通用-主题】如何修改主题色

### 需求
radio button 默认的橘色不适合，需要改为蓝色。
实现方法：通过修改less变量的主题色 `@primary` 来实现。

### 实现
1.  在 `src/styles/theme-custom/globals/theme.variables.less` 中声明 less 变量

    ```less
    // COMMON
    @primary: blue;
    ```

### 效果

### 原理
在 `globals/theme.variables.less` 中定义的这些变量会在 falcon-ui 编译过程中注入，并覆盖原有声明变量。
所有可修改变量可以在 `node_modules/falcon-ui/src/styles/globals/theme.variables.less` 中查看
建议可修改变量可参考附录《主题系统样式表》章节

## 【通用-主题】如何修改控件样式

### 背景
想要对某一个控件样式做全局调整，比如想要 radio button 的 label 成竖向排列。

### 实现
*   创建文件 `src/styles/component/radio.overrides.less`，并添加样式

    ```less
    .radio-item {
        flex-direction: column;
    }
    ```

### 效果

### 原理
falcon-ui 主题框架中，对每个组件设计留有覆盖机制，因此 `componet/xxx.overrides.less` 会被作为 css style 覆盖到控件 `<style></style>` 内部最下方，因此会将上方声明的相关样式做覆盖操作。

## 【通用-主题】如何利用内置主题变量

### 背景
falcon-ui 控件库主题十分统一，而封装custom 控件时如果理解并统一使用主题变量，后续风格调整会一起变化。
关于主题变量，建议使用几组全局控制开关，包括色彩、字体、字号、边距、间距、圆角、线条。
可参考附录《主题系统样式表》章节

### 实现
*   在 `src/styles/base.less` 中添加 falcon-ui 主题色变量引用

    ```less
    @import "falcon-ui/src/styles/theme.less";
    ```

*   在 index.vue 需要使用的页面引用 base.less

    ```css
    <style lang="less" scoped>
        @import "base.less";
        // ...
    </style>
    ```

*   在 css style 中使用样式变量，比如 `@background-color`

    ```vue
    <template>
        <div class="wrapper">
            <fl-radio :items="radioItems" v-model="radioValue" />
        </div>
    </template>
    <style lang="less" scoped>
        .wrapper {
            background-color: @background-color;
        }
    </style>
    ```

### 效果

## 附录：主题系统样式表

### 样式架构
*   色彩
*   字体
*   字号
*   边距
*   间距
*   圆角
*   线条

### 色彩

| 名称 | 色值 | 变量名 |
| :--- | :--- | :--- |
| 主题色 | #FF6A00 | `@primary` |
| 白色 | #FFFFFF | `@white` |
| 黑色 | #000000 | `@black` |
| 背景色 | #202731 | `@background-color` |
| 卡片背景色 | #343F50 | `@card-background-color` |
| 辅助色 | #48586F | `@secondary` |
| 按钮色 | #778AA7 | `@btn-background-color` |
| 功能色_荧光 | #7CFF00 | `@light-green` |
| 功能色_绿 | #2D901E | `@green` |
| 功能色_蓝 | #376FDB | `@blue` |
| 功能色_黄 | #FF9500 | `@yellow` |
| 功能色_紫 | #893FCD | `@purple` |
| 功能色_青 | #209AAD | `@cyan` |
| 功能色_红 | #EF4141 | `@red` |

**状态色彩：**
通过不透明度区分组件的状态（此配置不建议在可配置样式表中）
*   正常 - 100%
*   点击 - 60%
*   禁用 - 40%

### 字体
*   默认（中，英文，数字，符号）：阿里巴巴普惠体
*   特殊数字：Alibaba Sans 102

### 字号/字重（基于4寸屏）

| 类型 | 名称 | 字号 | 字重 | 变量名 |
| :--- | :--- | :--- | :--- | :--- |
| 标题 | 大标题 | 36 | Medium | `@font-size-title-large` `@font-weight-title-large` |
| | 中标题 | 32 | Medium | `@font-size-title-medium` `@font-weight-title-medium` |
| | 小标题 | 28 | Medium | `@font-size-title-small` `@font-weight-title-small` |
| 正文 | 正文 | 24 | Regular | `@font-size-content-large` `@font-weight-content-large` |
| | 二级正文 | 20 | Regular | `@font-size-content-medium` `@font-weight-content-medium` |
| | 辅助文 | 18 | Regular | `@font-size-content-small` `@font-weight-content-small` |

### 边距（基于4寸屏）

| 名称 | 值 | 变量名 |
| :--- | :--- | :--- |
| 极紧 | 8px | `@space-very-compact` |
| 紧凑 | 16px | `@space-compact` |
| 标准 | 24px | `@space-normal` |
| 宽松 | 32px | `@space-loose` |

### 间距（基于4寸屏）

| 名称 | 值 | 变量名 |
| :--- | :--- | :--- |
| 极紧 | 8px | `@gap-very-compact` |
| 紧凑 | 16px | `@gap-compact` |
| 标准 | 24px | `@gap-normal` |
| 宽松 | 32px | `@gap-loose` |

### 圆角（基于4寸屏）

| 名称 | 值 | 变量名 |
| :--- | :--- | :--- |
| 小 | 8PX | `@radius-small` |
| 中 | 16PX | `@radius-medium` |
| 标准 | 24PX | `@radius-normal` |
| 大 | 32PX | `@radius-large` |

### 边框

| 名称 | 值 | 变量名 |
| :--- | :--- | :--- |
| 小 | 1PX | `@border-small` |
| 中 | 2PX | `@border-medium` |
| 标准 | 3PX | `@border-normal` |
| 大 | 4PX | `@border-large` |
        ```
          </div>
        </details>
        <details>
          <summary>📄 CSS动画</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # CSS动画

应用支持通过Animation模块动画，也支持css3动画。

Animation动画参考文档：页面模块-Animation

## css3动画

**注意：**

1.  暂时不支持多个动画属性分开设置。如果有类似的需求，需要写成两个css样式。
2.  transition不支持多个属性连写。

### CSS

```css
/*多个属性动画使用不同的配置,请分开使用多个样式写*/
.transition-demo1 {
  transition: width 2000ms cubic-bezier(0.25, 0.1, 0.25, 1);
}
.transition-demo2 {
  transition: height 2000ms linear;
}
/*下面的写法在打包时会提示错误*/
.transition-demo-wrong {
  transition: width 2000ms linear, height 1000ms ease; /*编译提示错误*/
}
```

### 示例

#### Vue

```vue
<template>
  <div class="main">
    <text class="title">css动画</text>
    <div class="anim-obj-wrap">
      <div :class="'anim-obj ' +  cls "></div>
    </div>
    <div class="button-wrap">
      <div class="button" @click="doAnim('anim-obj-move')">
        <text class="button-text">移动</text>
      </div>
      <div class="button" @click="doAnim('anim-obj-rotate')">
        <text class="button-text">旋转</text>
      </div>
      <div class="button" @click="doAnim('anim-obj-size')">
        <text class="button-text">尺寸</text>
      </div>
      <div class="button" @click="doAnim('anim-obj-bg')">
        <text class="button-text">背景颜色</text>
      </div>
      <div class="button" @click="reset()">
        <text class="button-text">复原</text>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  data() {
    return {
      cls: "",
    };
  },
  methods: {
    doAnim(cls) {
      this.cls = cls;
    },
    reset() {
      this.cls = '';
    },
  },
};
</script>

<style scoped>
.main {
  justify-content: flex-start;
  align-items: flex-start;
  position: relative;
  background: #f7f7f7;
  display: flex;
  flex-direction: column;
}
.button-text {
  font-size: 22px;
  line-height: 40px;
  color: #ffffff;
}
.button-wrap {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-items: center;
  background-color: #eee;
  margin-bottom: 20px;
}
.button {
  background-color: #7B72E9;
  border-color: #7B72E9;
  border-radius: 50px;
  height: 40px;
  padding: 0 20px;
  display: flex;
  align-items: center;
  color: white;
  margin: 5px;
}
.title {
  font-weight: bold;
  font-size: 30px;
}
.anim-obj-wrap {
  height: 400px;
  display: flex;
  position: relative;
}
.anim-obj {
  position: relative;
  width: 100px;
  height: 100px;
  margin-left: 200px;
  margin-top: 200px;
  background-color: #999;
  justify-content: center;
  transition: all 2000ms cubic-bezier(0.25, 0.1, 0.25, 1);
}
.anim-obj-move {
  margin-left: 0px;
  margin-top: 0px;
}
.anim-obj-bg {
  background-color: #7b72e9;
}
.anim-obj-size {
}
</style>
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 CSS单元</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # CSS单元

## 长度单位

支持以下长度单位：
*   `px`
*   `rpx`
*   `%`
*   `vw`
*   `vh`

**注意：**
*   不支持类似 `em`、`rem`、`pt` 这样的 CSS 标准中的其他长度单位。
*   单位 `px` 不可省略，否则在 H5 环境无法正确渲染。

对于不希望受屏幕宽度和 viewPortWidth 影响的尺寸，可使用 `rpx`、`%`、`vw`、`vh`。

## 数值单位

除了长度单位外，还有数值单位，仅仅一个数值，后面没有 `px` 等单位。用于 `opacity`、`lines`、`flex` 等属性指定一个纯数值。

有时值必须是整数，例如：`lines`。

## 颜色单位

支持多种颜色单位：
*   精简写法的十六进制，如 `#f00`
*   十六进制，如 `#ff0000`
*   RGB， 如 `rgb(255, 0, 0)`
*   RGBA，如 `rgba(255, 0, 0, 0.5)`
*   色值关键字，如 `red`

```css
.classA {
  /* 3-chars hex */
  color: #0f0;
  /* 6-chars hex */
  color: #00ff00;
  /* rgba */
  color: rgb(255, 0, 0);
  /* rgba */
  color: rgba(255, 0, 0, 0.5);
  /* transparent */
  color: transparent;
  /* Basic color keywords */
  color: orange;
  /* Extended color keywords */
  color: darkgray;
}
```

**注意：**
*   只有上面列出的颜色格式被支持，其他颜色格式均**不**被支持。
*   6-chars hex 16进制颜色值是性能最好的颜色使用方式。除非有特殊原因，请使用 6-chars hex 格式。

## 颜色关键字列表

| 颜色名 | 十六进制RGB值 |
| :--- | :--- |
| black | #000000 |
| gray | #808080 |
| white | #FFFFFF |
| red | #FF0000 |
| purple | #800080 |
| green | #008000 |
| yellow | #FFFF00 |
| blue | #0000FF |
        ```
          </div>
        </details>
        <details>
          <summary>📄 Less支持</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # Less支持

## 简介

框架预置Less支持。Less 是一门 CSS 预处理语言，它扩展了 CSS 语言，增加了变量、Mixin、函数等特性。详细的Less内容可参考：[Less官方文档](https://lesscss.org/)。

## 启用Less

在框架工程中启动Less只需要设置vue文件中的style标签的lang属性为less即可。支持内联和外联。

```html
<template>
...
</template>
<script>
...
</script>
<!-- 外联less -->
<style lang="less" src="./default.less" scoped></style>
<!-- 内联less -->
<style lang="less" scoped>
@text-color-default:#cccccc;
.text-common{
  color:@text-color-default;
}
.text-content{
  .text-common();
}
</style>
```

## 注意

*   Less `@import` 路径不支持`@`开头的缩写，请使用相对路径引入。
        ```
          </div>
        </details>
        <details>
          <summary>📄 多分辨率自适应机制</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 多分辨率自适应机制

## 使用场景
在实际产品中，屏幕尺寸会有变化，因此需要在编码阶段考虑到自适应不同屏幕尺寸。

> 注：此处屏幕尺寸的宽高比例不能有较大差异，否则从UED角度需要重新出设计稿。

## 方法1：【推荐】px 布局 + viewport指定
**原理**：按照 viewport 与 device width 进行比例转换。

**使用方法**：
1. 在 app.js 中设定 viewport
```javascript
/**
 * 应用生命周期:应用启动. 初始化完成时回调,全局只触发一次.
 * @param {Object} options 启动参数
 */
onLaunch(options) {
    super.onLaunch(options)
    this.setViewPort(800)
}
```
2. 在 app 中布局均使用 px，宽度按照 800px 来布局。

**如何理解**：
可以假设我们在一张指定 viewport 为 800px 的窗口中布局，最终系统框架会将这个虚拟布局尺寸缩放到最终的设备宽高尺寸上。

## 方法2：rpx 布局
**原理**：按照 viewport 固定为 750rpx 与 device width 进行比例转换，即 750rpx 等于 100% device width。

**使用方法**：
与网页、其他小程序框架一样使用方式。
```css
.test {
    width: 750rpx;
}
```
这种方式如果设计稿为 750px 宽度那么极为方便，否则就需要对设计稿的px 值做等比转换。

## 方法3：vw/vh 机制布局
**原理**：按照 device width 做百分比计算后的像素。

**使用方法**：
与网页 vw vh 使用方式一样。
- vw：相对于 device width 的宽度百分比，视窗宽度为 100vw。
- vh：相对于 device width 的高度百分比，视窗高度为 100vh。

```css
.test {
    position: fixed;
    top: 0;
    height: 50vh;
    width: 100vw;
}
```

## 效果
```
HAAS-UI
TEST WIDTH RPX 750RPX
TEST WIDTH RPX 750PX
TEST VH VW
POSITION: FIXED;
BOTTOM: 0;
HEIGHT: 50VH;
WIDTH;100VW;
```

## 代码例子：
稍后补充。
        ```
          </div>
        </details>
        <details>
          <summary>📄 字库配置</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 字库配置

**关键字**：字体，字库

框架支持系统级自定义字库配置（暂不支持App自定义字体）。

## 配置

`fontconfig`遵循XML配置（`/etc/miniapp/resources/fonts/fonts.xml`），配置内容参考如下：

```xml
<?xml version="1.0" encoding="utf-8"?>
<familyset>
    <!-- first font is default -->
    <family name="PuHuiTi">
        <font weight="400">Alibaba-PuHuiTi-Regular.otf</font>
        <font weight="800">Alibaba-PuHuiTi-Bold.otf</font>
    </family>
    <family name="宋体">
        <font weight="400">songti.ttf</font>
    </family>
    <!-- icon fonts -->
    <family name="falcon-icons">
        <font weight="400">falcon-icons.ttf</font>
    </family>
    <!-- Fallback fonts -->
    <family>
        <font weight="400">Fallback-R.ttf</font>
        <font weight="600">Fallback-M.ttf</font>
        <font weight="800">Fallback-B.ttf</font>
    </family>
</familyset>
```

### family

`family`对应一个具有相同字符集的字体系列，可包含多个字体文件分别对应多个字重。其中xml中的第一个`family`配置为系统默认字体（在前端未设置`font-family`，或者`family`在`fonts.xml`中未定义时，使用默认字体）。

### name

字体系列名称，前端CSS可使用【`font-family: PuHuiTi`】样式来指定使用的字体。`name`为空表示该字体为`fallback`备选字体，作用是在指定字体中找不到对应字符时，会尝试降级到备选字体中（备选字体可以多个，按照xml配置顺序来查找）。

### font

表示字体系列中的一个字体文件，可支持相对路径（`/etc/miniapp/resources/fonts/`目录）和绝对路径配置。其中`font-weight`表示字体的字重，对应前端CSS中的`font-weight`样式（100-900，bold，默认400），配置不全的会按照就近原则选择一个最接近的。

## 前端使用示例

```html
<template>
    <div>
        <text style="font-family: 宋体">宋体显示</text>
    </div>
</template>
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 文本样式</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 文本样式

文本类组件共享一些通用样式，这类组件目前包括 `<text>`。

## 属性

### color
`color {color}`：文字颜色，支持如下字段：
* RGB（`rgb(255, 0, 0)`）
* RGBA（`rgba(255, 0, 0, 0.5)`）
* 十六进制（`#ff0000`）；精简写法的十六进制（`#f00`）
* 色值关键字（`red`）

### font-size
`font-size {number}`：文字大小。

### font-style
`font-style {string}`：字体类别。可选值 `normal` | `italic`，默认为 `normal`。

### font-weight
`font-weight {string}`：字体粗细程度
* 可选值: `normal`, `bold`, `100`, `200`, `300`, `400`, `500`, `600`, `700`, `800`, `900`
* `normal` 等同于 400, `bold` 等同于 700；
* 默认值: `normal`；
* iOS 支持 9 种 font-weight值；Android 仅支持 400 和 700, 其他值会设为 400 或 700
* 类似 `lighter`, `bolder` 这样的值暂时不支持

### text-decoration
`text-decoration {string}`：字体装饰，可选值 `none` | `underline` | `line-through`，默认值为 `none`。
> 只支持 `<text>`

### text-align
`text-align {string}`：对齐方式。可选值 `left` | `center` | `right`，默认值为 `left`。

### font-family
`font-family {string}`：设置字体。这个设置**不保证**在不同平台，设备间的一致性。如所选设置在平台上不可用，将会降级到平台默认字体。暂不支持加载自定义字体。（系统字体配置参考【字库配置】）

### text-overflow
`text-overflow {string}`：设置内容超长时的省略样式。可选值 `clip` | `ellipsis`
> 只支持 `<text>`

### lines
`lines {number}`: 正整数，指定最大文本行数，默认值为0，表示不限制最大行数。如果文本不够长，实际展示行数会小于指定行数。

### line-height
`line-height {length}`：正整数，每行文字高度。
`line-height` 是 top 至 bottom 的距离。

```
TOP
ASCENT
BASELINE
MY TEXT LINE
DESCENT
BOTTOM
MY TEXT LINE 2.
```

`line-height` 与 `font-size` 没有关系，因为 `line-height` 被 top 和 bottom 所限制，`font-size` 被 glyph 所解析。`line-height` 和 `font-size` 相等一般会导致文字被截断。

### word-wrap
`word-wrap {string}`: `break-word` | `normal` | `anywhere`。对 Weex 来说 `anywhere` 表示在以字符为最小元素做截断换行，其它值或不指定该属性，都以英文单词为单位进行换行。
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 事件</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 全局事件</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 全局事件

全局事件用于监听持久性事件，如设备蓝牙事件。全局事件分类：

1.  **用户自定义事件**：由 JavaScript 通过 `$falcon.trigger()` 手动触发。作用于当前应用。
2.  **底层全局事件**：由底层 JSAPI 模块触发的全局事件，作用于全局。

> **提示**：使用全局事件功能，需要严格遵循 on/off 流程，否则会存在内存泄漏问题。

## 全局事件监听

全局事件通过 `$falcon.on(eventName, callback)` 方法监听，指定监听名称与回调函数即可，例如：

```javascript
$falcon.on('blechanged', (e) => {
  console.log(e.type, e.timestamp, e.data);
});
```

全局事件回调参数包含以下信息：

```json
{
  "type": "String", // 事件类型
  "timestamp": "Integer", // 事件触发时间
  "data": "Object" // 事件参数
}
```

通过 `$falcon.off(eventName, callback)` 方法注销监听。

```javascript
const callback = (e) => {
  // do something
};

// 注册监听
$falcon.on('blechanged', callback);

// 取消监听
$falcon.off('blechanged', callback);
```

如果 `callback` 不传或者传空值 (`null`, `undefined`)，则取消当前应用所有 `eventName` 对应的监听，如：

```javascript
$falcon.off("blechanged");
```

## 全局事件触发

### 1) 用户自定义事件

应用中可手动触发全局事件。通过 JavaScript 手动触发的全局事件仅限作用于当前应用，不会被其他应用收到。

```javascript
const eventOptions = { data1, data2 };
$falcon.trigger('eventName', eventOptions);
```

### 2) 底层全局事件

底层可通过事件接口触发全局事件，底层触发的全局事件所有应用都可监听收到。

```cpp
// jsapi模块发送事件的方法
ariver::iot::ExtensionProxyBase* extensionProxy = ariver::iot::getJSApiExtensionProxy();
// 事件名，参数json
extensionProxy->sendCustomEvent("packageUninstalled", "{\"appId\":\"" + appId + "\"}");
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 左滑事件</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 左滑事件

## 监听左滑事件

```javascript
// 禁用左滑退出
this.$page.$npage.setSupportBack(false);

// 监听backpressed（左滑事件）
let backpressed = () => {
    this.$page.finish();
};
this.$page.$npage.on("backpressed", backpressed);

// 注销
this.$page.$npage.off("backpressed", backpressed);
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 模块事件</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 模块事件

模块事件由扩展模块通过 C++ 接口触发。当前所有存活的应用皆可监听。

对应 JSAPI 中的不同模块，可按照模块维度进行事件监听。

## 示例

### C++

```cpp
// jsapi模块发送事件的方法
ariver::iot::ExtensionProxyBase* extensionProxy = ariver::iot::getJSApiExtensionProxy();
// 模块名，事件名，参数json
extensionProxy->sendCustomEvent("pm", "packageUninstalled", "{\"appId\":\"" + appId + "\"}");
```

### JavaScript

```javascript
// 前端使用方法
const pm = $falcon.jsapi.pm;
const callback = (e) => {
    // do something
}
pm.on('packageInstalled', callback);
pm.off('packageInstalled', callback);
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 通用组件事件</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 通用组件事件

Dom和组件事件遵循Vue标准,可参考Vue事件文档: [事件处理](), [组件自定义事件]()

## 事件对象

所有事件对象统一拥有以下参数:

```json
{
  "type": "String", // 事件类型
  "target": "Object", // 事件对象,交互事件为事件触发的元素.全局/模块事件为事件触发对应的全局/模块对象
  "timestamp": "number", // 事件触发时间戳
  "data": "Object|Number..." // 可选,事件触发时需要传递的参数,参数名[data]根据事件类型可自定义
}
```

## 1. 交互响应事件

用户点击/长按/滑动等操作元素的事件,作用于元素/组件上。

| 事件类型 | 说明 |
|---------|------|
| click | 点击事件 |
| longpress | 长按事件 |

```vue
<template>
  <div class="btn" @click="handleClick">
    <text class="btn-text">click</text>
  </div>
  <div class="btn" @longpress="handleLongPress">
    <text class="btn-text">longpress</text>
  </div>
</template>

<script>
export default {
  methods: {
    handleDisappear(e, id) {
      console.log(`${e.timestamp} ${id} disappear ${e.direction}`);
    },
    handleClick(e) {
      console.log(e.timestamp + ': click');
    },
    handleLongPress(e) {
      console.log(e.timestamp + ': longpress');
    }
  }
};
</script>
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 页面跳转</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 页面跳转

## 简介
页面/应用跳转相关 API，主要封装在一个全局对象 `$falcon` 上。

## $falcon
`$falcon` 下的接口均为全局接口。

### $falcon.navTo
页面/应用跳转。跳转到应用内指定页面或者其他应用的指定页面。

**方法原型:**
```javascript
$falcon.navTo(target:String, options:Object)
```

**参数说明:**
*   **target**: 需要跳转的页面名称或目标页面的 uri。
    *   **应用内跳转**：参数为**页面名称**。如：`"index"`, `"xxPage"`。
    *   **应用间跳转**：参数为 uri，如：`"falcon://appid/index?param1=xxx"`。schema 必须为 falcon，**推荐采用 Category 跳转方式**对比 appid 的跳转会更加通用。
    *   **Category 跳转**：参数同**应用间跳转**，如 `"falcon://{Category}/index?param1=xxx"`，具体 Category 如何配置参考 `local_packages.json` 中的 amr 包配置（请确保配置 Category 的唯一性）。
*   **options**: 页面参数，跳转到下一个页面所需要的参数。如果 target 为 uri 且带有参数，则会合并后传给下一个页面；参数格式为 key/value 字符串 JSON 格式。
    *   KV 格式：如 `{ data: "data1" }`

**示例代码:**
```javascript
$falcon.navTo("pageName", { data: "data1" }); //应用内页面跳转，pageName为应用页面名
$falcon.navTo("falcon://HOME/index"); //Category跳转，且指定index页面
$falcon.navTo("falcon://8180000000000020"); //不推荐通过appid进行，应用间跳转，默认跳转到index页面
$falcon.navTo("falcon://8180000000000020/index"); //不推荐通过appid进行，应用间跳转，且指定index页面

// 新页面page.js在onLoad生命周期中接收参数
onLoad(options) { }
```

### $falcon.closeApp
在应用不需要继续存活时退出应用。

**方法原型:**
```javascript
$falcon.closeApp()
```

### $falcon.closePageByName
根据页面名称关闭页面。

**方法原型:**
```javascript
$falcon.closePageByName(pageName:String)
```

**参数说明:**
*   **pageName**: 页面名称。页面名称可通过对应页面实例的 `$pageName` 属性获取。

### $falcon.closePageById
根据页面 id 关闭页面。

**方法原型:**
```javascript
$falcon.closePageById(pageId:String)
```

**参数说明:**
*   **pageId**: 页面 id。页面 id 可通过对应页面实例的 `$pageId` 属性获取。

## App
App 的方法不是全局，是应用对应的 App 类实例化后的方法。该实例可通过 `$falcon.$app` 属性获取。

### app.finish
退出当前应用。

**方法原型:**
```javascript
app.finish()
```

**示例代码:**
```javascript
$falcon.$app.finish(); // 可以通过$falcon全局对象获取当前应用App对象的实例
```

## Page
Page 的方法不是全局的，只在页面生命周期内以及页面的根组件和子组件中可访问。组件中可通过在组件中 `this.$page` 或者 `$falcon.getPage(component:Component)` 获取当前页面的引用。page 实例中存放的对象如下：

| 属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| `$falcon` | Object | 全局 `$falcon` 引用 |
| `$root` | Object | 页面根组件引用 |
| `$pageName` | String | 页面名称 |
| `$pageId` | String | 页面 id |
| `loadOptions` | Object | 页面首次启动时的参数(注1) |
| `newOptions` | Object | 页面重新启动时的参数(注1) |
| `setRootComponent` | function | 设置根组件 |
| `loadOptions` | Object | 页面首次启动时的参数 |
| `newOptions` | Object | 页面重复打开时的参数(只保留最后一次打开参数) |
| `finish` | function | 关闭当前页面 |
| `$animation` | Object | 页面动画模块(jsapi扩展,只支持容器中使用) |
| `$dom` | Object | 页面节点信息模块(jsapi扩展,只支持容器中使用) |

**示例代码:**
```javascript
// 使用方式例如
methods: {
  onShow() {
    console.log('loadOptions: ', this.$page.loadOptions)
    console.log('newOptions: ', this.$page.newOptions)
    this.page_loadOptions = this.$page.loadOptions
    this.page_newOptions = this.$page.newOptions
  }
}
```

### page.setRootComponent
设置页面的根组件。每个页面都需要对应一个根组件。此方法在页面的 onLoad 生命周期中调用。

**方法原型:**
```javascript
page.setRootComponent(component:Component)
```

**参数说明:**
*   **component**: Vue 组件对象。

**示例代码:**
```javascript
import IndexComponent from './index.vue';

class PageIndex extends $falcon.Page {
  onLoad(options) {
    super.onLoad(options);
    this.setRootComponent(IndexComponent);
  }
}
export default PageIndex;
```

### page.finish
关闭当前页面。

**方法原型:**
```javascript
page.finish()
```

## 关键字
页面跳转、路由
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 前端基础组件</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 ＜canvas＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # canvas

## 简介

组件用来创建画布组件。

## 基本用法

```html
<template>
  <div>
    <canvas ref="canvas" />
  </div>
</template>

<script>
export default {
  mounted() {
    let context = typeof createCanvasContext === 'function' ? createCanvasContext(this.$refs.canvas) : this.$refs.canvas.getContext('2d');
    context.fillStyle = "red";
    context.fillRect(0, 0, 100, 100);
  },
};
</script>
```

## 子组件

不支持子组件。

## 属性

| key | 类型 | 描述 | 默认值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| width | number | 画布宽度 | 300 | 需>0 |
| height | number | 画布高度 | 150 | 需>0 |

## 样式

### 通用样式

支持所有通用样式。
*   盒模型
*   flexbox 布局
*   position
*   opacity
*   background-color

## 事件

*   通用事件
    支持所有通用事件。

## canvas接口

### 颜色、样式

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| fillStyle | Color/Gradient | 设置填充绘画的样式 | ctx.fillStyle="white"\|grad |
| strokeStyle | Color/Gradient | 设置线条绘画的样式 | ctx.strokeStyle="white"\|grad |
| createLinearGradient() | function | 创建线性渐变（用在画布内容上） | ctx.createLinearGradient(x0,y0,x1,y1)<br>x0: 渐变开始点的 x 坐标<br>y0: 渐变开始点的 y 坐标<br>x1: 渐变结束点的 x 坐标<br>y1: 渐变结束点的 y 坐标 |
| createRadialGradient() | function | 创建放射状/环形的渐变（用在画布内容上） | ctx.createRadialGradient(x0,y0,r0,x1,y1,r1)<br>x0: 渐变开始圆的 x 坐标<br>y0: 渐变开始圆的 y 坐标<br>r0: 开始圆的半径<br>x1: 渐变结束圆的 x 坐标<br>y1: 渐变结束圆的 y 坐标<br>r1: 结束圆的半径 |
| addColorStop() | function | 规定渐变对象中的颜色和停止位置 | grad.addColorStop(stop,color)<br>stop: 介于 0.0 与 1.0 之间的值，表示渐变中开始与结束之间的位置。<br>color: 在结束位置显示的 CSS 颜色值 |

### 线条样式

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| lineWidth | number | 设置线条宽度，单位px | ctx.lineWidth=5 |
| lineCap | Enum | 设置线条的结束线帽样式 | ctx.lineCap='butt\|round\|square' |
| lineJoin | Enum | 设置线条交叉的拐角样式 | ctx.lineJoin='bevel\|round\|miter' |
| miterLimit | number | 设置最大斜接长度，lineJoin=miter时生效 | ctx.miterLimit=10 |

### 矩形

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| rect() | function | 创建矩形 | ctx.rect(0,0,100,100) |
| fillRect() | function | 填充矩形 | ctx.fillRect(0,0,100,100) |
| strokeRect() | function | 绘制矩形(无填充) | ctx.strokeRect(0,0,100,100) |
| clearRect() | function | 清除矩形像素 | ctx.clearRect(0,0,100,100) |

### 路径

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| fill() | function | 填充当前路径 | ctx.fill() |
| stroke() | function | 绘制当前路径(无填充) | ctx.stroke() |
| beginPath() | function | 开始或重置一条路径 | ctx.beginPath() |
| closePath() | function | 关闭路径(从当前点到起始点) | ctx.closePath() |
| moveTo() | function | 把路径移动到画布中的指定点，不创建线条 | ctx.moveTo(100,100) |
| lineTo() | function | 添加一个新点，然后在画布中创建从该点到最后指定点的线条 | ctx.lineTo(100,200) |
| clip() | function | 从原始画布剪切当前路径尺寸的区域 | ctx.clip() |
| quadraticCurveTo() | function | 创建二次贝塞尔曲线 | ctx.quadraticCurveTo(cpx,cpy,x,y)<br>cpx: 控制点的 x 坐标<br>cpy: 控制点的 y 坐标<br>x: 结束点的 x 坐标<br>y: 结束点的 y 坐标 |
| bezierCurveTo() | function | 创建三次贝塞尔曲线 | ctx.bezierCurveTo(cp1x,cp1y,cp2x,cp2y,x,y)<br>cp1x: 第一个控制点的 x 坐标<br>cp1y: 第一个控制点的 y 坐标<br>cp2x: 第二个控制点的 x 坐标<br>cp2y: 第二个控制点的 y 坐标<br>x: 结束点的 x 坐标<br>y: 结束点的 y 坐标 |
| arc() | function | 创建弧/曲线（用于创建圆形或部分圆） | ctx.arc(x,y,r,sAngle,eAngle)<br>x: 圆的中心的 x 坐标。<br>y: 圆的中心的 y 坐标。<br>r: 圆的半径。<br>sAngle: 起始角，以弧度计。（弧的圆形的三点钟位置是 0 度）。<br>eAngle: 结束角，以弧度计。 |
| arcTo() | function | 创建两切线之间的弧/曲线 | ctx.arcTo(x1,y1,x2,y2,r)<br>x1: 弧的起点的 x 坐标。<br>y1: 弧的起点的 y 坐标。<br>x2: 弧的终点的 x 坐标。<br>y2: 弧的终点的 y 坐标。<br>r: 弧的半径。 |

### 转换

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| scale() | function | 缩放当前绘图至更大或更小 | ctx.scale(0.5,0.5) |
| rotate() | function | 旋转当前绘图 | ctx.rotate(angle)<br>angle: 旋转弧度 |
| translate() | function | 平移当前绘图 | ctx.translate(10,10) |
| transform() | function | 替换绘图的当前转换矩阵 | ctx.transform(a,b,c,d,e,f)<br>a: 水平缩放绘图<br>b: 垂直倾斜绘图<br>c: 水平倾斜绘图<br>d: 垂直缩放绘图<br>e: 水平移动绘图<br>f: 垂直移动绘图 |
| setTransform() | function | 将当前转换重置为单位矩阵。然后运行 transform() | ctx.setTransform(a,b,c,d,e,f) |

### 文本

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| font | String | 设置当前文字样式 | ctx.font="font-style font-weight font-size"<br>font-style: normal/italic(可选)<br>font-weight: normal/bold(可选)<br>font-size: 字号，像素单位 |
| fillText() | function | 在画布上绘制“被填充的”文本 | ctx.fillText(text,x,y,maxWidth)<br>text: 文本字符串<br>x: 开始绘制文本的 x 坐标位置<br>y: 开始绘制文本的 y 坐标位置<br>maxWidth: 可选。允许的最大文本宽度，以像素计。 |
| strokeText() | function | 在画布上绘制文本（无填充） | ctx.strokeText(text,x,y,maxWidth)<br>同fillText |
| measureText() | function | 返回包含指定文本宽度的对象 | ctx.measureText(text).width<br>text: 要测量的文本字符串<br>result: width 测量的文本宽度 |

### 图像

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| drawImage | function | 向画布上绘制图像 | ctx.drawImage(img,sx,sy,swidth,sheight,x,y,width,height)<br>img: 要使用的图像(image标签)<br>sx: 可选。开始剪切的 x 坐标位置。<br>sy: 可选。开始剪切的 y 坐标位置。<br>swidth: 可选。被剪切图像的宽度。<br>sheight: 可选。被剪切图像的高度。<br>x: 在画布上放置图像的 x 坐标位置。<br>y: 在画布上放置图像的 y 坐标位置。<br>width: 可选。要使用的图像的宽度。（伸展或缩小图像）<br>height: 可选。要使用的图像的高度。（伸展或缩小图像） |

### 像素操作

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| createImageData | function | 创建新的、空白的 ImageData 对象 | ctx.createImageData(width,height) |
| getImageData() | function | 返回 ImageData 对象，该对象为画布上指定的矩形复制像素数据 | ctx.getImageData(x,y,width,height) |
| putImageData() | function | 把图像数据（从指定的 ImageData 对象）放回画布上 | ctx.putImageData(imgData,x,y)<br>x: ImageData 对象左上角的 x 坐标，以像素计。<br>y: ImageData 对象左上角的 y 坐标，以像素计。 |

### 合成

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| globalAlpha | Number | 设置当前透明值，全局生效 | ctx.globalAlpha=0.5 |
| globalCompositeOperation | Enum | 设置混合模式，新图像如何绘制到已有的图像上 | ctx.globalCompositeOperation="source-over\|source-atop\|source-in\|source-out\|destination-over\|destination-atop\|destination-in\|destination-out\|lighter\|copy\|xor"<br>source-over: 默认值，在目标图像上显示源图像。 |

### 其他

| 接口 | 类型 | 描述 | 备注 |
| :--- | :--- | :--- | :--- |
| save() | function | 保存当前环境的状态 | ctx.save() |
| restore() | function | 返回之前保存过的路径状态和属性 | ctx.restore() |
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜div＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<div>`

## 简介

`<div>` 是通用容器。

*   不要在 `<div>` 中直接添加文本，而要使用 `<text>` 组件。
*   目前默认 `<div>` 不可滚动。
*   要控制 `<div>` 的层级，建议不要超过14层，否则会很影响页面性能。

## 子组件

`<div>` 支持各种类型的子元素，包括 `<div>` 自己。

## 样式

*   **通用样式**
    支持所有 [通用样式](#)。

## 事件

*   **通用事件**
    支持所有 [通用事件](#)。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜image-frame＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<image-frame>`

## 简介
用于在界面中显示序列帧动画。

*   在代码中请使用 `<image-frame>` 标签。
*   序列帧图片需选用本地图片，不支持从网络下载。
*   `<image-frame>` 不支持内嵌子组件。

## 基本用法

```html
<image-frame ref="images" class="image"
:src="[require('../../images/0.png'),
require('../../images/1.png'),
require('../../images/3.png'),
require('../../images/5.png'),
require('../../images/6.png'),
require('../../images/7.png'),
require('../../images/8.png')]" />
```

## 子组件
`<image-frame>` 不支持子组件。

## 样式
支持**通用样式**。

> **WARNING**
> `width`, `height` 和 `src` 必须被提供，否则图片无法渲染。

## 属性

| 属性名 | 类型 | 值 | 默认值 |
| :--- | :--- | :--- | :--- |
| `src` | String数组 | `{imagePath }` | - |
| `auto-play` | Boolean | 是否自动播放 | `false` |
| `loop-count` | Integer | 循环播放次数 | `-1` (永久循环) |
| `interval` | Integer | 帧间隔(ms) | `100(ms)` |

## 图片扩展功能
详细见文档。

## 事件

*   **通用事件**。 参见 **通用事件**

### `end`
当动画运行结束时，`end` 事件将被触发。

#### 处理 `end` 事件
在 `<image-frame>` 标签上绑定 `end` 事件：

```html
<image-frame @end="onAnimationEnd"/>
```

增加事件处理函数：

```javascript
export default {
  methods: {
    onAnimationEnd (event) {
    }
  }
}
```

### `repeat`
当动画循环播放开始时，`repeat` 事件将被触发。

## 扩展

### `start()`
开始播放动画。

### `stop()`
停止播放动画。

### `pause()`
暂停动画。

### `resume()`
恢复播放动画。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜image＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<image>`

## 简介

用于在界面中显示单个图片。

*   在代码中请使用 `<image>` 标签。
*   默认框架提供 `mini-glide` 图片下载模块。
*   `<image>` 必须指定样式中的宽度和高度。
*   `<image>` 不支持内嵌子组件。

## 基本用法

```html
<template>
  <image style="width:500px;height:500px" src="https://vuejs.org/images/logo.png" />
</template>
```

## 子组件

`<image>` 不支持子组件。

## 样式

支持**通用样式**。

> **WARNING**
> `width`, `height` 和 `src` 必须被提供，否则图片无法渲染。

## 属性

| 属性名 | 类型   | 值                     | 默认值   |
| :----- | :----- | :--------------------- | :------- |
| resize | String | cover / contain / stretch | stretch  |
| src    | String | {URL / Base64 }        | -        |

### resize

*   `contain`：缩放图片以完全装入 `<image>` 区域，可能背景区部分空白。
*   `cover`：缩放图片以完全覆盖 `<image>` 区域，可能图片部分看不见。
*   `stretch`：**默认值**。按照 `<image>` 区域的宽高比例缩放图片。

`resize`属性和 `background-size` 的理念很相似。

### src

要显示图片的 URL，该属性是 `<image>` 组件的强制属性。如果要显示设备文件系统上的图片，可使用 `file://图片绝对路径` 来显示。

## 支持的图片格式

目前已支持的图片格式 **JPEG、PNG、GIF、BMP** 等图片格式。

### 网络图片

```html
<template>
  <image style="width:500px;height:500px" src="https://vuejs.org/images/logo.png" />
</template>
```

### 应用图片

```html
<template>
  <image class="image" resize="contain" :src="require('./images/image1.png')" />
</template>
```

### 本地图片

```html
<template>
  <img src="file:///userdata/DictPenData/userAvatar.jpeg" />
</template>
```

## 图片下载-图片扩展功能

某些业务需要将网上图片下载到本地，框架提供了将该图在CLI编译时下载到应用包编译中，解决无网络情况下图片加载问题。

```javascript
require('http://a.png?download')
```

## 图片转成Base64-图片扩展功能

某些业务希望能更快的加载图片，框架提供了将该图片在CLI编译时转换成Base64的功能，提升图片显示速度。

```javascript
require('http://a.png?base64')  //网络图片
// 或
require('./a.png?base64')      //本地图片
```

## 事件

*   **通用事件**。参见 **通用事件**。

### load

当加载完成 `src` 指定的图片时，`load` 事件将被触发。

**事件对象**:

*   `success`: {Boolean} 标记图片是否成功加载。
*   `size`: {Object} 加载的图片大小对象，属性列表：
    *   `naturalWidth`: {Number} 图片宽度，如果图片加载失败则为0。
    *   `naturalHeight`: {Number} 图片高度，如果图片加载失败则为0。

#### 处理 `load` 事件

在 `<image>` 标签上绑定 `load` 事件：

```html
<image @load="onImageLoad" src="path/to/image.png"></image>
```

增加事件处理函数：

```javascript
export default {
  methods: {
    onImageLoad (event) {
      if (event.success) {
        // Do something to hanlde success
      }
    }
  }
}
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜input＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<input>` 组件

## 简介
`<input>` 组件用来创建接收用户输入字符的输入组件。

`<input>` 组件的工作方式目前只支持 `password` 或普通文本，暂不支持比如 `url`，`email`，`tel` 等。

**注意**
此组件不支持 `click` 事件。请监听 `input` 或 `change` 来代替 `click` 事件。

## 子组件
`<input>` 不支持子组件。

## 属性

| key | 类型 | 描述 | 默认值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| type | string | 控件的类型 | text | 待完善 |
| value | string | 组件的默认内容 | | |
| placeholder | string | 提示用户可以输入什么。提示文本不能有回车或换行 | | |
| placeholderColor | Color | 提示文本的颜色 | | |
| autofocus | boolean | 布尔类型的数据，表示是否在页面加载时控件自动获得输入焦点 | | |
| maxLength | number | 一个数值类型的值，表示输入的最大长度 | | |
| cursorColor | Color | 设置光标的颜色 | rgba(255,255,255,0.5) | |
| cursorSize | number | 设置光标的宽度 | 2 | |
| softInputEnable | boolean | 开启、关闭输入法功能 | true | 如果设置为false则input获得焦点也不会弹出键盘 |

## 样式

*   **color {color}**
    字符颜色。默认值是 `#000`
*   **font-size {px}**
    字符颜色。默认值是 `32px`

### 通用样式
支持所有通用样式
*   盒模型
*   flexbox 布局
*   position
*   opacity
*   background-color

查看 [组件通用样式]

## 事件

*   **通用事件**
    支持所有 [通用事件]。
*   **input**
    当输入状态时，会不断触发。
    *   `@param value`: 当前文本。
*   **focus**
    当输入框获得焦点时。
*   **blur**
    当输入框丢失焦点时。

## 扩展

*   **appendText(text)**
    从当前光标位置追加文本。
*   **popBack()**
    从当前光标位置往前删除一个字符。
*   **commitText(text)**
    替换全部文本。
*   **commitComplete()**
    触发confirm事件。
*   **getCursorPosition(callback)**
    获取当前光标位置。
*   **setCursorPosition(index)**
    设置当前光标位置。

## 约束
目前不支持 `this.$el(id).value = ''` 这种方式改写 input value。只支持在 `<input>` 组件的 input、change 事件中改写。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜latex＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<latex>`

## 简介

`<latex>` 组件，用来渲染 latex 排版系统，可用于数学公式等场景。

`<latex>` 不支持子组件。

语法参考：http://www.uinio.com/Math/LaTex/

> **提示**：该组件需要框架版本为 V1.6 版本。

## 样式

*   支持 **通用样式**。
*   支持 **文本样式**。
    *   `font-size`
    *   `line-spacing`
    *   `color`

## 属性

## 事件

*   支持 **通用事件**。

## 其他
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜lottie＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<lottie>`

## 简介
用于在界面中显示lottie矢量动画。

* 在代码中请使用 `<lottie>` 标签。
* lottie动画的json文件，只支持在本地加载。
* `<lottie>` 不支持内嵌子组件。
* 暂不支持image、text以及mask功能。

## 基本用法

```html
<lottie
  class="lottie"
  ref="lottie"
  lottieFile="/assets/lotties/lottie.json"
  :loopCount="100"
  :autoPlay="true"
/>
```

## 子组件
`<lottie>` 不支持子组件。

## 样式
支持**通用样式**。

## 属性

| 属性名 | 类型 | 值 | 默认值 |
| :--- | :--- | :--- | :--- |
| lottie-file | String | {jsonPath } | - |
| auto-play | Boolean | 是否自动播放 | false |
| loop-count | Integer | 循环播放次数 | -1 (永久循环) |
| speed | float | 动画播放速度 | 1 |

## 事件

* **通用事件** . 参见 **通用事件**

### end
当动画运行结束时，`end` 事件将被触发。

#### 处理 end 事件
在 `<lottie>` 标签上绑定 `end` 事件：

```html
<lottie @end="onAnimationEnd"/>
```

增加事件处理函数：

```javascript
export default {
  methods: {
    onAnimationEnd (event) {
    }
  }
}
```

### repeat
当动画循环播放开始时，`repeat` 事件将被触发。

### cancel
当动画取消开始时，`cancel` 事件将被触发。

## 扩展

### play()
开始播放动画。

### cancel()
停止播放动画。

### pause()
暂停动画。

### resume()
恢复播放动画。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜modal＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # modal

## 简介
用于显示系统弹窗，应用处于后台也可弹出，可基于此组件封装全局弹出的alert、confirm、toast。

* 在代码中请使用 `<modal>` 标签。
* `<modal>` 支持内嵌其它子组件。
* `<modal>` 默认具有fixed样式。

## 基本用法

```html
<modal
  class="modal"
  v-if="shown"
>
  <slot />
<modal>
```

## 样式
支持**通用样式**。

## 属性

| 属性名 | 类型 | 值 | 默认值 |
| :--- | :--- | :--- | :--- |
| floating | Boolean | 是否浮窗(浮窗背后的内容是否可见)<br>如果为false，弹出后，后面的应用和页面会走onHide生命周期，并且显示为不可见。 | true |
| focusable | Boolean | 是否抢占用户焦点，alert/confirm类的弹窗应使用true，toast类使用false | true |
| cancelable | Boolean | 是否支持滑动退出(左滑) | true |
| touch-through | Boolean | 是否支持触屏事件穿透 | false |

## 事件

* **通用事件** . 参见 **通用事件**

### dismiss
当弹窗退出时，`dismiss` 事件将被触发。

## 扩展
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜qrcode＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<qrcode>`

## 简介
用于在界面中显示二维码。

* 在代码中请使用 `<qrcode>` 标签。
* `<qrcode>` 不支持内嵌子组件。

## 基本用法

```html
<qrcode
  class="qrcode"
  ref="qrcode"
  value="https://haas.iot.aliyun.com/haasui"
  level="Q"
  color="black"
/>
```

## 子组件
`<qrcode>` 不支持子组件。

## 样式
支持**通用样式**。

> **WARNING**
> `value` 必须被提供。

## 属性

| 属性名 | 类型 | 值 | 默认值 |
| :--- | :--- | :--- | :--- |
| value | String | 二维码内容 | - |
| level | enum(L/M/Q/H) | 纠错级别 | Q |
| color | Color | 二维码颜色 | black |

## 事件
* **通用事件**。 参见 [通用事件](#)

## 扩展
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜richtext＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 简介
组件用来创建图文混排的富文本组件。

# 基本用法

```html
<template>
  <div>
    <richtext
      @itemclick="listener"
      @click="click"
      style="color: red; font-size: 20px; lines: 3; background-color: green;"
    >
      <span pseudoRef="1" style="text-decoration: line-through; font-size: 50">
        linklink
      </span>
      <image style="width: 150;" src="https://ftp.bmp.ovh/imgs/2019/12/b29a4c1207bdd187.jpeg">
      </image>
      <span style="font-size: 42; color: yellow">
        TAOBAO
      </span>
      <image src="https://g.alicdn.com/iot_miniapp/falcon-ui-demo/0.0.13/images/gif.gif" pseudo-ref="23">
      </image>
      <span style="text-decoration: underline; background-color: blue; line-height: 40px">
        轻量级小程序是一套用在嵌入式设备上的轻量级应用开发框架,是AliOS Things系统上推荐的应用&显示框架,目前是JS开发为主、C/C++开发为辅.
      </span>
    </richtext>
  </div>
</template>
<script>
  module.exports = {
    methods: {
      listener: function (foo) {
        console.log('itemclick', foo.pseudoRef);
      },
      click(e) {},
    },
  };
</script>
```

# 子组件
富文本组件可以内嵌 `<span>` `<br>` `<image>` `<div>`。同时它也支持 `<span>` `<br>` `<image>` `<div>` 的嵌套。

只有 `<span>`, `<br>`, `<image>` 和 `<div>` 可以包含在 `<richtext>` 标签里。

* `<span>` 会被显示为 `display:inline`。
* `<image>` 会被显示为 `display:inline-block`。
* `<div>` 会被显示为 `display:block`。

`<richtext>` 的子节点分两种类型：
* `<span>` `<div>` 可以再包含孩子节点。
* `<image>` `<br>` 不能再包含孩子节点。
* 富文本组件自身不能嵌套。

# 样式

## 通用样式
支持所有通用样式：
* 盒模型
* flexbox 布局
* position
* opacity
* background-color
* lines 最大行数
* text-overflow: ellipsis 设置文字超出省略号显示样式

## 子组件样式
富文本和它下面的 `<span>`, `<br>`, `<image>` 只支持有限的样式。

### `<span>`, `<br>` 和 `<richtext>`
* 可以被继承
  * color
  * font-family
  * font-size
  * font-style
  * font-weight
  * line-height
* 不可被继承
  * background-color

### `<span>`
* 可以被继承
  * text-decoration: none | underline | line-through, 默认值是 none

### `<richtext>`
* 不可被继承
  * lines: 最大行数，必须为正数。

### `<image>`
* 不可被继承
  * width
  * height

# 属性

## 支持的图片格式
目前已支持的图片格式 JPEG、PNG、GIF、BMP 等图片格式。

# 事件
* 通用事件
  * 支持所有 **通用事件**。
* itemclick
  * 触发时机是:
    * 选中的组件包含 `pseudo-ref` 属性，`pseudo-ref` 会作为参数传回来。
    * 若多个嵌套节点上均包含 `itemclick` 事件，则只有最外层节点上的 `itemclick` 会被触发。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜scroller＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<scroller>`

## 简介

`<scroller>` 是一个容纳子组件进行横向或竖向滚动的容器组件。如果你的组件需要进行滚动，可以将 `<scroller>` 当作根元素或者父元素使用，否则页面无法滚动。

`<scroller>` 需要显式的设置其宽高，可使用 `position: absolute;` 定位或 `width`、`height` 设置其宽高值。

```html
<template>
  <scroller class="scroller">
    <div class="row" v-for="row in rows" :key="row.id">
      <text class="text">{{row.name}}</text>
    </div>
  </scroller>
</template>
<script>
  export default {
    data () {
      return {
        rows: []
      }
    },
    created () {
      for (let i = 0; i < 80; i++) {
        this.rows.push({id: i, name: 'row ' + i})
      }
    },
  }
</script>
```

## 子组件

`<scroller>` 支持任意类型的组件作为其子组件。

## 属性

| 参数 | 说明 | 类型 | 默认值 |
| :--- | :--- | :--- | :--- |
| `show-scrollbar` | 控制是否出现滚动条 | `boolean` | `true` |
| `scroll-direction` | 控制滚动的方向 | `string` (`horizontal` 或者 `vertical`) | `vertical` |
| `over-scroll` | 控制是否支持边界回弹效果 | `px` (最大回弹距离) | `0`, 不支持回弹 |
| `over-fling` | 快划场景是否支持边界回弹效果 | `px` (最大回弹距离) | `0`, 不支持回弹 |
| `scrollEventInterval` | 控制 scroll 事件的触发间隔 | `number` (ms 毫秒) | `100` |
| `scrollable` | 是否可滚动 | `boolean` | `true` |
| `scrollWithAnimation` | 滚动是否有动画 | `boolean` | `true` |
| `scrollAnimationDuration` | 滚动动画时间 | `number` | `-1`, 按内容尺寸自动计算 |

`scroll-direction` 定义了 scroller 的滚动方向，样式表属性 `flex-direction` 定义了 scroller 的布局方向，两个方向必须一致。例如：

*   `scroll-direction` 的默认值是 `vertical`，`flex-direction` 的默认值是 `column`；
*   当需要一个水平方向的 scroller 时，使用 `scroll-direction="horizontal"` 和 `flex-direction: row`;
*   当需要一个竖直方向的 scroller 时，使用 `scroll-direction="vertical"` 和 `flex-direction: column`，由于这两个值均是默认值，当需要一个竖直方向的 scroller 时，这两个值可以不设置。

## 事件

*   **`scroll`**
    列表发生滚动时将会触发该事件，事件的默认触发频率为 10px，即列表每滚动 10px 触发一次，可通过属性 `offset-accuracy` 设置抽样率。事件中 `Event` 对象有以下属性:

    | 属性 | 说明 | 类型 |
    | :--- | :--- | :--- |
    | **contentSize** | 列表的内容尺寸 | `Object` |
    | &nbsp;&nbsp;`width` | 列表内容宽度 | `number` |
    | &nbsp;&nbsp;`height` | 列表内容高度 | `number` |
    | **contentOffset** | 列表的偏移尺寸 | `Object` |
    | &nbsp;&nbsp;`x` | x 轴上的偏移量 | `number` |
    | &nbsp;&nbsp;`y` | y 轴上的偏移量 | `number` |

*   **`scrollstart`**
    **H5 暂不支持该事件**，当列表开始滚动时触发，当前的内容高度和列表偏移会在 callback 中返回，示例参见 Demo。

*   **`scrollend`**
    **H5 暂不支持该事件**，与 `scrollstar` 类似，当列表结束滚动时触发，当前的内容高度和列表偏移会在 callback 中返回，示例参见 Demo。

    > 注：`scrollEventInterval` 可控制 scroll 事件的调用间隔，有时候不需要频发检测scroll滚动位置，特别是影响mvvm值发生重绘的场景，适当调大该值可

*   **`loadmore`**
    该事件在滑动触底的时候触发，用于增量更新数据。
    *   如果在切换数据后，内容变稍后，会使得该事件不会触发，此时需要调用组件的 `resetLoadmore` 方法

## 扩展

### `scrollToElement(ref, options)`

`<scroller>` 支持滚动到某个指定的元素，可通过 `dom.scrollToElement()` 滚动到指定元素位置。

**参数**

*   `ref {refs}`：指定目标节点
*   `options {Object}`：可选项，属性为：
    *   `offset {number}`：一个到其可见位置的偏移距离，默认是 0

**例如**

```vue
<template>
  ...
  <text ref="target-item">滚动到的目标元素</text>
</template>
<script>
  ...
  // 方法逻辑，滚动到目标元素
  onScrollTo () {
    const ref = this.$refs['target-item']
    this.$page.$dom.scrollToElement(ref, { offset: 0 })
  },
  ...
</script>
```

### `resetLoadmore` 方法

**功能**：在使用 `loadmore` 的过程中，因为切换数据导致 scroller 内容变短后，需要使用该方法来重置 `loadmore` 的内部状态，使得再次触底时可以正常触发 `loadmore` 事件。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜seekbar＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # seekbar

## 简介
`<seekbar>` 组件用来创建带滑动进度条的组件。

## 基本用法

```html
<template>
  <seekbar class="seekbar" />
</template>

<script>
export default {
};
</script>

<style scoped>
.seekbar {
  width: 424px;
  height: 100px;
}
</style>
```

## 子组件
`<seekbar>` 不支持子组件。

## 属性

| key | 类型 | 描述 | 默认值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| min | number | 最小值 | 0 | 需>=0 |
| max | number | 最大值 | 100 | 需>min |
| step | number | 步长，值必须大于0，并可被(max - min)整出 | 1 | >0 |
| value | number | 当前值 | 0 | |
| active-color | Color | 前景色(已拖动的进度条) | #108ee9 | |
| background-color | Color | 进度条背景色 | #dddddd | |
| track-size | number | 进度条高度 | 4 | |
| handle-size | number | 滑块大小 | 22 | |
| handle-color | Color | 滑块颜色 | #108ee9 | |
| handle-inner-color | Color | 滑块内环颜色 | transparent | |

## 样式

### 通用样式
支持所有通用样式
* 盒模型
* flexbox 布局
* position
* opacity
* background-color

查看 [组件通用样式](#)

## 事件
* **通用事件** 支持所有[通用事件](#)。
* **changing** 拖动过程中触发的事件，会不断触发。
  * @param value: 当前值。
* **change** 完成一次拖动后触发。
  * @param value: 当前值。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜slider＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # Slider

## 简介
Slider 组件用于在一个页面中展示多个图片，在前端这种效果被称为轮播图。默认的轮播间隔为3秒。

## 基本用法

```html
<template>
  <div>
    <slider class="slider" interval="3000" auto-play="true">
      <div class="frame" v-for="img in imageList">
        <image class="image" resize="cover" :src="img.src"></image>
      </div>
    </slider>
  </div>
</template>

<style scoped>
.image {
  width: 700px;
  height: 700px;
}
.slider {
  margin-top: 25px;
  margin-left: 25px;
  width: 700px;
  height: 700px;
}
.frame {
  width: 700px;
  height: 700px;
  position: relative;
}
</style>

<script>
export default {
  data() {
    return {
      imageList: [
        {
          src: 'https://gd2.alicdn.com/bao/uploaded/i2/T14H1LFwBcXXXXXXXX_!!0-item_pic.jpg'
        },
        {
          src: 'https://gd1.alicdn.com/bao/uploaded/i1/TB1PXJCJFXXXXciXFXXXXXXXXXX_!!0-item_pic.jpg'
        },
        {
          src: 'https://gd3.alicdn.com/bao/uploaded/i3/TB1x6hYLXXXXXazXVXXXXXXXXXX_!!0-item_pic.jpg'
        }
      ]
    }
  }
}
</script>
```

## 子组件
支持任意类型的组件作为其子组件。

## 属性

| key | 类型 | 描述 | 默认值 | 备注 |
|-----|------|------|--------|------|
| vertical | boolean | 是否纵向轮播 | false | 常量，初始化后不可修改 |
| auto-play | boolean | 是否自动开始轮播 | false | |
| interval | number | 轮播间隔，单位ms | 3000ms | |
| index | number | 设置显示slider的第几个页面 | 0 | |
| show-indicators | boolean | 是否显示页面指示器 | false | |
| infinite | boolean | 设置是否无限循环轮播（头尾相连） | false | |
| scrollable | boolean | 是否支持手势滑动 | true | |
| duration | number | 滚动时间 | 500 毫秒 | |
| item-color | Color | 指示器默认颜色 | rgba(255,255,255,0.5) | |
| item-selected-color | Color | 指示器选中状态的颜色 | white | |
| item-size | number | 指示器大小 | 10 | |
| enable-acceleration | boolean | 是否支持快速多页滑动 | false | |
| scale-factor | number | 缩放因子，按照距离中心元素的距离进行缩放 | 1.f | (0,1] |
| previous-margin | px | 左边(上边)元素露出大小 | 0px | |
| next-margin | px | 右边(下边)元素露出大小 | 0px | |

## 样式
- **通用样式**：支持所有通用样式。

## 事件
- **通用事件**：支持所有通用事件。
- **change**：当轮播索引改变时，触发该事件。该事件给前端的参数中含有 `index` 表示当前切换到的序号。

## 方法
- **slideTo(int index, bool smooth)**
  - 手动控制轮播位置，index 为轮播位置，smooth控制切换时是否有动画效果
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜textarea＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<textarea>` 组件

## 简介
组件用来创建接收用户输入字符的多行输入组件。

组件的工作方式目前只支持普通文本，暂不支持比如 url，email，tel 等。

## 基本用法

```html
<template>
  <textarea
    ref="textarea"
    class="textarea"
    @input="input"
    @confirm="confirm"
    :value="value"
  />
</template>
<script>
export default {
  data() {
    return {
      value: 'textarea'
    };
  },
  mounted() {
    this.$refs.textarea.getCursorPosition(r => {
      console.log('cursorPosition=' + r);
    });
  },
  methods: {
    input(v) {
      console.log('input', v);
    },
    confirm(v) {
      console.log('confirm', v);
    }
  },
};
</script>
```

## 子组件
不支持子组件。

## 属性

| key | 类型 | 描述 | 默认值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| value | string | 组件的默认内容 | | |
| autofocus | boolean | 布尔类型的数据，表示是否在页面加载时控件自动获得输入焦点 | | |
| cursorColor | Color | 设置光标的颜色 | rgba(255,255,255,0.5) | |
| cursorSize | number | 设置光标的宽度 | 2 | |
| softInputEnable | boolean | 是否支持弹出系统软键盘 | false | |
| showCursor | boolean | 是否持续显示光标 | false | |

## 样式

*   **color {color}** 字符颜色。默认值是 `#000`
*   **font-size {px}** 字符颜色。默认值是 32px

### 通用样式
支持所有通用样式
*   盒模型
*   flexbox 布局
*   position
*   opacity
*   background-color

查看组件通用样式

## 事件

*   **通用事件** 支持所有通用事件。
*   **input** 当输入状态时，会不断触发。
    *   @param value: 当前文本。
*   **confirm** 当输入完成时触发。
    *   @param value: 当前文本。
*   **focus** 当输入框获得焦点时。
*   **blur** 当输入框丢失焦点时。

## 约束
目前不支持 `this.$el(id).value = ''` 这种方式改写 input value。只支持在 `<textarea>` 组件的 input、change 事件中改写。

## 扩展

### appendText(text)
从当前光标位置追加文本。

### popBack()
从当前光标位置往前删除一个字符。

### commitText(text)
替换全部文本。

### commitComplete()
触发confirm事件。

### getValue(callback)
获取当前文本内容。

### getCurrentLineValue(callback)
获取当前光标位置所在行的文本内容。

### getCurrentLine(callback)
获取当前光标位置所在行的文本内容，文本index以及光标在当前行中的位置。

> **callback 返回数据示例：**
> `{"text":"some text","index":0,"cursor":18}`

### setCurrentLineValue(text)
设置当前光标所在行的文本内容，会触发文本重新布局。

### replaceValue(pos, len, text)
替换部分文本内容。

### getCursorPosition(callback)
获取当前光标位置。

### setCursorPosition(index)
设置当前光标位置。

### moveCursor(direction)
移动指针位置，prev/next/up/down。
        ```
          </div>
        </details>
        <details>
          <summary>📄 ＜text＞</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # `<text>` 组件

## 简介
组件，用来将文本按照指定的样式渲染出来。

不支持子组件。

`<text>` 里直接写文本头尾空白会被过滤，如果需要保留头尾空白字符，暂时只能通过数据绑定的方式，见下面动态文本。

## 样式
*   支持 **通用样式**。
*   支持 **文本样式**。

## 属性

| 属性名 | 类型 | 描述 | 默认值 |
| :--- | :--- | :--- | :--- |
| `marquee` | boolean | 单行文本overflow的扩展效果，文字跑马灯效果（需配合`lines:1`样式） | `false` |
| `marquee-speed` | px | 跑马灯运动速度(px/second) | `30px` |
| `marquee-gap` | px | 跑马灯尾首衔接的间距 | 1/3文本组件宽度 |

## 动态文本
下列代码片段可以实现文字内容和JS变量的绑定。

```html
<template>
  <div>
    <text>{{content}}</text>
  </div>
</template>
<script>
  module.exports = {
    data: function(){
      return {
        content: "Weex is an cross-platform development solution that builds high-performance, scalable native applications with a Web development experience. Vue is a lightweight and powerful progressive front-end framework."
      }
    }
  }
</script>
```

## 事件
*   支持 **通用事件**。

## 其他

### 文字高度
文字高度的计算规则比较复杂，但大致上遵循以下优先级进行计算，排在前面的优先级最高。

1.  文字节点的 `max-height` / `min-height` 样式。
2.  文字节点的 `flex` 属性且文字的父节点上有 `flex-direction:column` 样式。
3.  文字节点的 `height` 样式。
4.  文字节点的 `align-items:stretch` 如果文字父节点有 `flex-direction:row` 样式。
5.  文字内容和文字本身的样式。
6.  其他相关CSS属性。
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary><strong>📁 模块调用</strong></summary>
      <div style="margin-left: 20px;">
        <details>
          <summary>📄 $dom模块</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # $dom模块

## scrollToElement

**语法**：`$dom.scrollToElement(ref, options)`

**行为**：页面滚动到该 ref 元素位置（left或top位置）

**参数**：

- `ref`：用 vue ref 特性持有的元素节点
- `options`
  - `smooth`：滚动行为是否支持过程动画
  - `offset`：滚动的位置增加的偏移像素

## getComponentRect

**语法**：`this.$page.$dom.getComponentRect(ref)`

**行为**：获取 ref 元素的布局信息

**示例**：

```json
{
  "result": true,
  "size": {
    "left": 601,
    "top": 1,
    "right": 801,
    "bottom": 49,
    "width": 200,
    "height": 48
  }
}
```
        ```
          </div>
        </details>
        <details>
          <summary>📄 如何使用页面模块</summary>
          <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
        ```markdown
        # 如何使用页面模块

页面模块可在页面对象 `$page` 中获取。

例如：

```javascript
mounted() {
  this.$page.$dom.getComponentRect($refs.someRef)
}
```
        ```
          </div>
        </details>
      </div>
    </details>
    <details>
      <summary>📄 全局&应用对象</summary>
      <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
    ```markdown
    # 全局&应用对象

## 全局对象($falcon)

全局唯一暴露对象: `$falcon`

该对象上挂载全局信息、应用信息、应用基类、接口、事件相关内容。每个应用拥有一个独立的 `$falcon` 实例。应用中任意位置可访问 `$falcon` 对象。

该实例下挂载的对象和方法：

| 属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| 应用基类 | App | Object | 应用基类 |
| Page | Object | 页面基类 |
| 应用全局 | env | Object | 当前的全局环境信息 |
| $app | Object | 当前应用的app.js实例。 |

*   全局事件见文档
*   页面跳转见文档

## 应用对象($app)

应用对象存放于工程 `src` 目录下的 `app.js` 中，继承 `$falcon.App`。应用启动时框架会初始化出一个 `App` 实例。该实例可通过 `$falcon.$app` 访问。

实例存放的对象和基类方法：

| 属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| $meta | Object | 应用meta信息引用 |
| launchOptions | Object | 应用启动参数 |
| $falcon | Object | 当前应用的$falcon对象引用 |

### $app下的方法说明

`$app` 方法可在任意能够获取 `app` 对象的位置使用（页面或组件中）。

#### app.finish()

关闭当前应用。

## 页面对象($page)

页面对象由 `app-meta.js` 中的 `pages` 属性定义。每个 `pages` 属性对应一个页面。在页面启动时由框架实例化。

组件中可通过在组件中 `this.$page` 或者 `$falcon.getPage(component:Component)` 获取当前页面的引用。

实例中存放的对象如下：

| 属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| $root | Object | 页面根组件引用 |
| $pageName | String | 页面名称 |
| $pageId | String | 页面id |
| loadOptions | Object | 页面首次启动时的参数（*注1） |
| newOptions | Object | 页面重新启动时的参数（*注1，只保留最后一次打开参数） |
| setRootComponent | function | 设置根组件 |
| $animation | Object | 页面动画模块（jsapi扩展，只支持容器中使用） |
| $dom | Object | 页面节点信息模块（jsapi扩展，只支持容器中使用） |

> *注1：页面首次启动参数为 `onLoad` 生命周期回调时获取的参数。页面重新启动参数为 `onNewOptions` 生命周期回调时获取的参数。

#### page.setRootComponent(component)

设置页面的根组件。每个页面都需要对应一个根组件。此方法在页面的 `onLoad` 生命周期中调用。

*   `component`: Vue组件对象

#### page.finish()

关闭当前页面。

## 四. 组件对象

每个页面都有一个根组件（Vue Component），根组件与页面绑定，是页面渲染的基础，页面中通过 `$page.$root` 可获取到根组件。

所有组件（包括根组件及所有子组件）中包含以下对象：

| 属性 | 类型 | 说明 |
| :--- | :--- | :--- |
| $app | Object | 当前应用实例 |
| $page | Object | 组件所在页面实例 |
| $falcon | Object | 当前应用的$falcon对象引用 |

## 五. 其他全局变量

| 全局变量名 | 类型 | 说明 |
| :--- | :--- | :--- |
| $workspace | string | 当前应用的安装目录 |
| $dataDir | string | 当前应用数据目录 |
| $appid | string | 当前应用的appid |

`$workspace` 可用于读取assets时拼接路径使用，比如应用根目录的assets目录会被打包到amr中，assets中的sound.mp3文件可以按照如下方法获得路径：

```javascript
const soundMp3 = `${$workspace}/assets/sound.mp3`
```

`$dataDir` 在存储应用私有数据时使用，例如按照如下方法获得app私有存储路径：

```javascript
const savePath = `${$dataDir}/downloads/tmp.txt`
```
    ```
      </div>
    </details>
    <details>
      <summary>📄 应用生命周期</summary>
      <div style="margin-left: 20px; padding: 10px; background-color: #f8f9fa; border: 1px solid #e9ecef; border-radius: 5px;">
    ```markdown
    # 应用生命周期

## 介绍
应用由多个页面组成。应用运行的各个阶段，可通过应用对象和页面对象获得应用生命周期回调。

> **注：** 通过VSCode创建工程后，对应生命周期代码已被创建。

## 应用
应用入口为 `app.js`。应用启动时会先执行该脚本。执行后导出一个继承自 `$falcon.App` 的子类。容器在拿到该类后开始执行应用生命周期。

应用生命周期：`onLaunch`、`onShow`、`onHide`、`onDestroy`。

```javascript
class App extends $falcon.App {
    /**
     * 构造函数，应用生命周期内只构造一次
     */
    constructor() {
        super();
    }

    /**
     * 应用生命周期：应用启动。初始化完成时回调，全局只触发一次。
     * @param {Object} options 启动参数
     */
    onLaunch(options) {
        super.onLaunch(options);
    }

    /**
     * 应用生命周期，应用启动或应用从后台切换到前台时触发
     */
    onShow() {
        super.onShow();
    }

    /**
     * 应用生命周期：应用退出前或者应用从前台切换到后台时触发
     */
    onHide() {
        super.onHide();
    }

    /**
     * 应用生命周期：应用销毁前触发
     */
    onDestroy() {
        super.onDestroy();
    }
}
export default App;
```

## 应用全局配置
应用配置文件为 `app.json`。

在应用配置文件中指定当前页面名称及页面路径。配置项为一个 `Object`。`key` 为页面名称，`value` 为页面路径。

页面可以为一个继承自 `$falcon.Page` 的 js，也可以直接指定一个 vue 组件。

每个应用必须配置一个 index 页面。该页面为应用启动时的默认页面。

```json
{
    "pages": {
        "index": "pages/index/index.js",
        "page2": "pages/page2/page2.vue"
    }
}
```

## 页面
应用的每个页面对应一个 `Page` 实例。如果在应用配置中页面路径直接指向一个 `Vue` 组件，框架会为页面自动创建一个对应的 `Page` 实例。

页面启动时先执行对应的页面 `[page].js` 脚本，得到导出的 `Page` 对象后执行页面的生命周期。

> **注：** 页面展示以前（生命周期 `onShow` 被调用之前）需要通过 `setRootComponent` 方法给页面设置根组件（在生命周期 `onLoad` 中设置）。

```javascript
import IndexComponent from './index.vue';

class PageIndex extends $falcon.Page {
    /**
     * 构造函数，页面生命周期内只执行一次
     */
    constructor() {
        super();
    }

    /**
     * 页面生命周期：首次启动
     * @param {Object} options 页面启动参数
     */
    onLoad(options) {
        super.onLoad(options);
        // 在onLoad方法中设置当前页面的根组件
        this.setRootComponent(IndexComponent);
    }

    /**
     * 页面生命周期：页面重新进入
     * 其他应用或者系统通过$falcon.navTo()方法重新启动页面。可以通过这个回调拿到新启动的参数
     * @param {Object} options 重新启动参数
     */
    onNewOptions(options) {
        super.onNewOptions(options);
    }

    /**
     * 页面生命周期：页面进入前台
     */
    onShow() {
        super.onShow(); //调用父类生命周期
        //onLoad生命周期之后，可以调用到根组件的方法
        this.$root.sayHello();
    }

    /**
     * 页面生命周期：页面进入后台
     */
    onHide() {
    }

    /**
     * 页面生命周期：页面卸载
     */
    onUnload() {
    }
}
export default PageIndex;
```
    ```
      </div>
    </details>
  </div>
</details>

## 使用说明

1. 点击目录标题（📁 图标）可展开/折叠查看详细内容
2. 点击文件标题（📄 图标）可查看文件详细内容
3. 所有技术规范均已按照原始目录结构组织
4. 文档内容已去除无关样式和脚本，只保留核心技术规范

---

*文档生成时间：2026-04-20*  
*总文件数：69*  
*支持展开/折叠的HTML结构*

> 注意：本文件包含HTML的`<details>`和`<summary>`标签，需要在支持这些标签的Markdown查看器中查看（如GitHub、GitLab、VSCode等）。
