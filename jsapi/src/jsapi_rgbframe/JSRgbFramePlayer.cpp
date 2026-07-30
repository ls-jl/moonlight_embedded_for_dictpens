#include "JSRgbFramePlayer.h"

#include "quickjs/quickjs.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace JQUTIL_NS;

namespace rgbframe {

namespace {

constexpr uint32_t RGBFRAME_MAGIC = 0x4647524dU;
constexpr uint32_t RGBFRAME_VERSION = 1;
constexpr uint32_t RGBFRAME_FORMAT_BGRA8888 = 1;
constexpr uint32_t RGBFRAME_FORMAT_RGBA8888 = 2;
const char *MOONLIGHT_PID_FILE = "/tmp/moonlight-rk3562/moonlight-miniapp.pid";
const char *RK_OTG_MODE_PATH = "/sys/devices/platform/ff740000.usb2-phy/otg_mode";

struct DrmScreenSize {
    int width = 0;
    int height = 0;
};

static std::string joinPath(const std::string &base, const std::string &name)
{
    if (base.empty()) {
        return name;
    }
    if (base[base.size() - 1] == '/') {
        return base + name;
    }
    return base + "/" + name;
}

static bool ensureDir(const std::string &path)
{
    if (mkdir(path.c_str(), 0777) == 0 || errno == EEXIST) {
        chmod(path.c_str(), 0777);
        return true;
    }
    std::fprintf(stderr, "mkdir %s failed: %s\n", path.c_str(), std::strerror(errno));
    return false;
}

static bool ensureDirRecursive(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    std::string current;
    size_t pos = 0;
    if (path[0] == '/') {
        current = "/";
        pos = 1;
    }

    while (pos <= path.size()) {
        size_t slash = path.find('/', pos);
        std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!part.empty()) {
            if (!current.empty() && current[current.size() - 1] != '/') {
                current += "/";
            }
            current += part;
            if (!ensureDir(current)) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }

    return true;
}

static bool setRkOtgMode(const char *mode)
{
    int fd = open(RK_OTG_MODE_PATH, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "open %s failed: %s\n", RK_OTG_MODE_PATH, std::strerror(errno));
        return false;
    }

    const size_t len = std::strlen(mode);
    ssize_t written = write(fd, mode, len);
    int savedErrno = errno;
    close(fd);

    if (written != static_cast<ssize_t>(len)) {
        std::fprintf(stderr, "write %s=%s failed: %s\n", RK_OTG_MODE_PATH, mode, std::strerror(savedErrno));
        return false;
    }

    std::fprintf(stderr, "RK OTG: set %s\n", mode);
    return true;
}

static bool writeTextFileIfExists(const std::string &path, const char *value)
{
    if (access(path.c_str(), F_OK) != 0) {
        return false;
    }

    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    const size_t len = std::strlen(value);
    const ssize_t written = write(fd, value, len);
    close(fd);
    return written == static_cast<ssize_t>(len);
}

static void tuneUsbRuntimePower()
{
    writeTextFileIfExists("/sys/module/usbcore/parameters/autosuspend", "-1");

    DIR *dir = opendir("/sys/bus/usb/devices");
    if (!dir) {
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string powerDir = std::string("/sys/bus/usb/devices/") + entry->d_name + "/power/";
        writeTextFileIfExists(powerDir + "control", "on");
        writeTextFileIfExists(powerDir + "level", "on");
        writeTextFileIfExists(powerDir + "autosuspend", "-1");
        writeTextFileIfExists(powerDir + "autosuspend_delay_ms", "-1");
    }

    closedir(dir);
}

static void applyStreamingKeepAlive()
{
    std::system("hal-screen keep >/dev/null 2>&1");
    std::system("hal-screen bright_time 600 >/dev/null 2>&1");
    std::system("iwconfig wlan0 power off >/dev/null 2>&1");
    std::system("iw dev wlan0 set power_save off >/dev/null 2>&1");
    writeTextFileIfExists("/sys/class/net/wlan0/power/control", "on");
    tuneUsbRuntimePower();
}

static std::string parentDir(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

static bool copyFile(const std::string &src, const std::string &dst, mode_t mode)
{
    int inFd = open(src.c_str(), O_RDONLY);
    if (inFd < 0) {
        std::fprintf(stderr, "open %s failed: %s\n", src.c_str(), std::strerror(errno));
        return false;
    }

    int outFd = open(dst.c_str(), O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (outFd < 0) {
        std::fprintf(stderr, "open %s failed: %s\n", dst.c_str(), std::strerror(errno));
        close(inFd);
        return false;
    }

    char buffer[16384];
    bool ok = true;
    ssize_t n;
    while ((n = read(inFd, buffer, sizeof(buffer))) > 0) {
        char *ptr = buffer;
        while (n > 0) {
            ssize_t written = write(outFd, ptr, n);
            if (written <= 0) {
                std::fprintf(stderr, "write %s failed: %s\n", dst.c_str(), std::strerror(errno));
                ok = false;
                break;
            }
            ptr += written;
            n -= written;
        }
        if (!ok) {
            break;
        }
    }
    if (n < 0) {
        std::fprintf(stderr, "read %s failed: %s\n", src.c_str(), std::strerror(errno));
        ok = false;
    }

    close(inFd);
    if (fsync(outFd) < 0) {
        ok = false;
    }
    close(outFd);
    chmod(dst.c_str(), mode);
    return ok;
}

static void replaceSymlink(const std::string &target, const std::string &linkPath)
{
    unlink(linkPath.c_str());
    if (symlink(target.c_str(), linkPath.c_str()) < 0) {
        std::fprintf(stderr, "symlink %s -> %s failed: %s\n", linkPath.c_str(), target.c_str(), std::strerror(errno));
    }
}

static bool prepareMoonlightRuntime(const std::string &runtimePath, const std::string &workdir)
{
    if (runtimePath.empty()) {
        return true;
    }

    if (!ensureDirRecursive(workdir) || !ensureDirRecursive(joinPath(workdir, "libgamestream"))) {
        return false;
    }

    const mode_t exeMode = 0777;
    const mode_t libMode = 0777;
    bool ok = true;
    ok &= copyFile(joinPath(runtimePath, "moonlight-rk3562"), joinPath(workdir, "moonlight-rk3562"), exeMode);
    ok &= copyFile(joinPath(runtimePath, "moonlight"), joinPath(workdir, "moonlight"), exeMode);
    ok &= copyFile(joinPath(runtimePath, "libmoonlight-rk.so"), joinPath(workdir, "libmoonlight-rk.so"), libMode);
    ok &= copyFile(joinPath(joinPath(runtimePath, "libgamestream"), "libgamestream.so.2.7.1"),
                   joinPath(joinPath(workdir, "libgamestream"), "libgamestream.so.2.7.1"),
                   libMode);
    ok &= copyFile(joinPath(joinPath(runtimePath, "libgamestream"), "libmoonlight-common.so.2.7.1"),
                   joinPath(joinPath(workdir, "libgamestream"), "libmoonlight-common.so.2.7.1"),
                   libMode);

    replaceSymlink("libgamestream.so.4", joinPath(joinPath(workdir, "libgamestream"), "libgamestream.so"));
    replaceSymlink("libgamestream.so.2.7.1", joinPath(joinPath(workdir, "libgamestream"), "libgamestream.so.4"));
    replaceSymlink("libmoonlight-common.so.4", joinPath(joinPath(workdir, "libgamestream"), "libmoonlight-common.so"));
    replaceSymlink("libmoonlight-common.so.2.7.1", joinPath(joinPath(workdir, "libgamestream"), "libmoonlight-common.so.4"));

    return ok && access(joinPath(workdir, "moonlight-rk3562").c_str(), X_OK) == 0;
}

struct SharedFrameHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    volatile uint32_t frameNo;
    uint32_t dataSize;
};

static uint8_t clampByte(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

static JSValue getObjectProperty(JSContext *ctx, JSValueConst obj, const char *name)
{
    if (!JS_IsObject(obj)) {
        return JS_UNDEFINED;
    }
    return JS_GetPropertyStr(ctx, obj, name);
}

static std::string getStringProperty(JSContext *ctx, JSValueConst obj, const char *name, const std::string &fallback)
{
    JSValue value = getObjectProperty(ctx, obj, name);
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, value);
        return fallback;
    }

    const char *str = JS_ToCString(ctx, value);
    if (!str) {
        JS_FreeValue(ctx, value);
        return fallback;
    }

    std::string result(str);
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, value);
    return result.empty() ? fallback : result;
}

static int getIntProperty(JSContext *ctx, JSValueConst obj, const char *name, int fallback)
{
    JSValue value = getObjectProperty(ctx, obj, name);
    if (!JS_IsNumber(value)) {
        JS_FreeValue(ctx, value);
        return fallback;
    }

    int32_t result = fallback;
    JS_ToInt32(ctx, &result, value);
    JS_FreeValue(ctx, value);
    return result > 0 ? result : fallback;
}

static int getIntPropertyAllowZero(JSContext *ctx, JSValueConst obj, const char *name, int fallback)
{
    JSValue value = getObjectProperty(ctx, obj, name);
    if (!JS_IsNumber(value)) {
        JS_FreeValue(ctx, value);
        return fallback;
    }

    int32_t result = fallback;
    JS_ToInt32(ctx, &result, value);
    JS_FreeValue(ctx, value);
    return result >= 0 ? result : fallback;
}

static bool getBoolProperty(JSContext *ctx, JSValueConst obj, const char *name, bool fallback)
{
    JSValue value = getObjectProperty(ctx, obj, name);
    if (!JS_IsBool(value)) {
        JS_FreeValue(ctx, value);
        return fallback;
    }

    bool result = JS_ToBool(ctx, value) == 1;
    JS_FreeValue(ctx, value);
    return result;
}

static pid_t readMoonlightPidFile()
{
    FILE *fp = std::fopen(MOONLIGHT_PID_FILE, "r");
    if (!fp) {
        return -1;
    }

    long pid = -1;
    if (std::fscanf(fp, "%ld", &pid) != 1) {
        pid = -1;
    }
    std::fclose(fp);

    return pid > 1 ? static_cast<pid_t>(pid) : -1;
}

static void writeMoonlightPidFile(pid_t pid)
{
    ensureDirRecursive(parentDir(MOONLIGHT_PID_FILE));
    FILE *fp = std::fopen(MOONLIGHT_PID_FILE, "w");
    if (!fp) {
        return;
    }
    std::fprintf(fp, "%ld\n", static_cast<long>(pid));
    std::fclose(fp);
}

static bool processExists(pid_t pid)
{
    return pid > 1 && (kill(pid, 0) == 0 || errno == EPERM);
}

static bool parseDrmModeLine(const std::string &line, DrmScreenSize &size)
{
    const char *mode = std::strstr(line.c_str(), "mode: \"");
    if (!mode) {
        return false;
    }

    int width = 0;
    int height = 0;
    if (std::sscanf(mode, "mode: \"%dx%d\"", &width, &height) != 2) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }

    size.width = width;
    size.height = height;
    return true;
}

static bool readDrmScreenSize(DrmScreenSize &size)
{
    std::ifstream state("/sys/kernel/debug/dri/0/state");
    if (state.good()) {
        std::string line;
        while (std::getline(state, line)) {
            if (parseDrmModeLine(line, size)) {
                return true;
            }
        }
    }

    std::ifstream dsiMode("/sys/class/drm/card0-DSI-1/modes");
    if (dsiMode.good()) {
        std::string mode;
        if (std::getline(dsiMode, mode)) {
            int width = 0;
            int height = 0;
            if (std::sscanf(mode.c_str(), "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
                size.width = width;
                size.height = height;
                return true;
            }
        }
    }

    return false;
}

static std::string readTextFile(const char *path)
{
    std::ifstream input(path);
    if (!input.good()) {
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static int normalizeRotationValue(int rotation, int fallback)
{
    switch (rotation) {
    case 0:
    case 90:
    case 180:
    case 270:
        return rotation;
    default:
        return fallback;
    }
}

static std::string jsonEscape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

}  // namespace

JSRgbFramePlayer::JSRgbFramePlayer()
    : running_(false)
    , screenKeepAliveRunning_(false)
{
}

JSRgbFramePlayer::~JSRgbFramePlayer()
{
    stopMoonlightProcess();
    stopScreenKeepAlive();
    stopWorker();
    unmapSharedFrame();
}

void JSRgbFramePlayer::OnGCCollect()
{
    stopWorker();
    unmapSharedFrame();
}

void JSRgbFramePlayer::setVideoInfo(JQFunctionInfo &info)
{
    if (info.Length() < 2 || !JS_IsNumber(info[0]) || !JS_IsNumber(info[1])) {
        info.GetReturnValue().ThrowTypeError("setVideoInfo(width, height) requires two numbers");
        return;
    }

    int32_t width = 0;
    int32_t height = 0;
    JS_ToInt32(info.GetContext(), &width, info[0]);
    JS_ToInt32(info.GetContext(), &height, info[1]);

    if (width <= 0 || height <= 0) {
        info.GetReturnValue().ThrowRangeError("invalid frame size %d x %d", width, height);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        width_ = width;
        height_ = height;
    }

    info.GetReturnValue().Set(true);
}

void JSRgbFramePlayer::setImageDataBuffer(JQFunctionInfo &info)
{
    if (info.Length() < 1) {
        info.GetReturnValue().ThrowTypeError("setImageDataBuffer(buffer) requires an ArrayBuffer");
        return;
    }

    size_t size = 0;
    uint8_t *buffer = JS_GetArrayBuffer(info.GetContext(), &size, info[0]);
    if (!buffer || size == 0) {
        info.GetReturnValue().ThrowTypeError("setImageDataBuffer expected ArrayBuffer");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        imageData_ = buffer;
        imageDataSize_ = size;
    }

    info.GetReturnValue().Set(static_cast<uint32_t>(size));
}

void JSRgbFramePlayer::setFrameInterval(JQFunctionInfo &info)
{
    if (info.Length() < 1 || !JS_IsNumber(info[0])) {
        info.GetReturnValue().ThrowTypeError("setFrameInterval(ms) requires a number");
        return;
    }

    int32_t interval = 0;
    JS_ToInt32(info.GetContext(), &interval, info[0]);
    interval = std::max(5, std::min(1000, interval));

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        frameIntervalMs_ = interval;
    }

    info.GetReturnValue().Set(interval);
}

void JSRgbFramePlayer::start(JQFunctionInfo &info)
{
    uint8_t *buffer = nullptr;
    size_t size = 0;
    int width = 0;
    int height = 0;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        buffer = imageData_;
        size = imageDataSize_;
        width = width_;
        height = height_;
    }

    if (!buffer || width <= 0 || height <= 0 || size < static_cast<size_t>(width) * height * 4) {
        info.GetReturnValue().ThrowInternalError("rgbframe is not ready");
        return;
    }

    startWorker();
    info.GetReturnValue().Set(true);
}

void JSRgbFramePlayer::stop(JQFunctionInfo &info)
{
    stopWorker();
    info.GetReturnValue().Set(true);
}

void JSRgbFramePlayer::isRunning(JQFunctionInfo &info)
{
    info.GetReturnValue().Set(running_.load());
}

void JSRgbFramePlayer::startMoonlight(JQFunctionInfo &info)
{
    pid_t pid = startMoonlightProcess(info);
    if (pid <= 0) {
        info.GetReturnValue().ThrowInternalError("failed to start moonlight");
        return;
    }

    info.GetReturnValue().Set(static_cast<int32_t>(pid));
}

void JSRgbFramePlayer::stopMoonlight(JQFunctionInfo &info)
{
    stopMoonlightProcess();
    info.GetReturnValue().Set(true);
}

void JSRgbFramePlayer::isMoonlightRunning(JQFunctionInfo &info)
{
    info.GetReturnValue().Set(isMoonlightProcessRunning());
}

void JSRgbFramePlayer::pairMoonlight(JQFunctionInfo &info)
{
    int result = pairMoonlightProcess(info);
    info.GetReturnValue().Set(static_cast<int32_t>(result));
}

void JSRgbFramePlayer::getDrmScreenSize(JQFunctionInfo &info)
{
    DrmScreenSize screen;
    if (!readDrmScreenSize(screen)) {
        info.GetReturnValue().Set("");
        return;
    }

    info.GetReturnValue().Set(std::to_string(screen.width) + "x" + std::to_string(screen.height));
}

void JSRgbFramePlayer::getSystemDisplayConfig(JQFunctionInfo &info)
{
    JSContext *ctx = info.GetContext();
    const std::string text = readTextFile("/etc/miniapp/resources/cfg.json");
    JSValue config = JS_UNDEFINED;
    if (!text.empty()) {
        config = JS_ParseJSON(ctx, text.c_str(), text.size(), "/etc/miniapp/resources/cfg.json");
        if (JS_IsException(config)) {
            JS_FreeValue(ctx, config);
            config = JS_UNDEFINED;
        }
    }

    const int width = getIntPropertyAllowZero(ctx, config, "width", 0);
    const int height = getIntPropertyAllowZero(ctx, config, "height", 0);
    const int frameworkRotation = normalizeRotationValue(
        getIntPropertyAllowZero(ctx, config, "direction", 0), 0);
    const int videoRotation = normalizeRotationValue(
        getIntPropertyAllowZero(ctx, config, "video_direction", frameworkRotation),
        frameworkRotation);
    const int touchRotation = normalizeRotationValue(
        getIntPropertyAllowZero(ctx, config, "tp_direction", videoRotation),
        videoRotation);
    const int touchOffsetX = getIntPropertyAllowZero(ctx, config, "tp_xoffset", 0);
    const int touchOffsetY = getIntPropertyAllowZero(ctx, config, "tp_yoffset", 0);
    const int fpsMax = getIntPropertyAllowZero(ctx, config, "fps_max", 0);
    const std::string touchDevice = getStringProperty(ctx, config, "tp", "");
    JS_FreeValue(ctx, config);

    int panelWidth = width;
    int panelHeight = height;
    if ((videoRotation == 90 || videoRotation == 270) &&
        panelWidth > 0 && panelHeight > 0 && panelWidth < panelHeight) {
        std::swap(panelWidth, panelHeight);
    }

    DrmScreenSize drm;
    const std::string drmMode = readDrmScreenSize(drm)
        ? std::to_string(drm.width) + "x" + std::to_string(drm.height)
        : "";
    const std::string panelSize = panelWidth > 0 && panelHeight > 0
        ? std::to_string(panelWidth) + "x" + std::to_string(panelHeight)
        : "";

    std::ostringstream output;
    output << "{"
           << "\"source\":\"" << (text.empty() ? "missing" : "system_cfg") << "\","
           << "\"width\":" << width << ","
           << "\"height\":" << height << ","
           << "\"panelSize\":\"" << jsonEscape(panelSize) << "\","
           << "\"frameworkRotation\":" << frameworkRotation << ","
           << "\"videoRotation\":" << videoRotation << ","
           << "\"touchRotation\":" << touchRotation << ","
           << "\"touchOffsetX\":" << touchOffsetX << ","
           << "\"touchOffsetY\":" << touchOffsetY << ","
           << "\"fpsMax\":" << fpsMax << ","
           << "\"drmMode\":\"" << jsonEscape(drmMode) << "\","
           << "\"touchDevice\":\"" << jsonEscape(touchDevice) << "\""
           << "}";
    info.GetReturnValue().Set(output.str());
}

pid_t JSRgbFramePlayer::startMoonlightProcess(JQFunctionInfo &info)
{
    stopMoonlightProcess();

    std::lock_guard<std::mutex> lock(processMutex_);

    JSContext *ctx = info.GetContext();
    JSValueConst options = info.Length() > 0 ? info[0] : JS_UNDEFINED;

    std::string workdir = getStringProperty(ctx, options, "workdir", "/tmp/moonlight-rk3562");
    std::string runtimePath = getStringProperty(ctx, options, "runtimePath", "");
    std::string binary = getStringProperty(ctx, options, "binary", runtimePath.empty() ? "/tmp/moonlight-rk3562/moonlight-rk3562" : "");
    std::string logPath = getStringProperty(ctx, options, "logPath", "/tmp/moonlight-rk3562/moonlight-drm.log");
    std::string keyDir = getStringProperty(ctx, options, "keyDir", joinPath(workdir, "keys"));
    std::string host = getStringProperty(ctx, options, "host", "");
    std::string app = getStringProperty(ctx, options, "app", "Desktop");
    int width = getIntProperty(ctx, options, "width", 1280);
    int height = getIntProperty(ctx, options, "height", 720);
    int fps = getIntProperty(ctx, options, "fps", 30);
    int bitrate = getIntProperty(ctx, options, "bitrate", 3000);
    int packetSize = getIntProperty(ctx, options, "packetSize", 1024);
    int rotate = getIntProperty(ctx, options, "rotate", 0);
    bool viewOnly = getBoolProperty(ctx, options, "viewOnly", true);
    bool otgHost = getBoolProperty(ctx, options, "otgHost", !viewOnly);
    bool stretch = getBoolProperty(ctx, options, "stretch", true);
    bool takeover = getBoolProperty(ctx, options, "takeover", true);
    std::string remote = getStringProperty(ctx, options, "remote", "yes");
    std::string touchMode = getStringProperty(ctx, options, "touchMode", "screen");
    std::string touchDevice = getStringProperty(ctx, options, "touchDevice", "");
    int touchRotation = normalizeRotationValue(
        getIntPropertyAllowZero(ctx, options, "touchRotation", rotate), rotate);
    int touchOffsetX = getIntPropertyAllowZero(ctx, options, "touchOffsetX", 0);
    int touchOffsetY = getIntPropertyAllowZero(ctx, options, "touchOffsetY", 0);
    if (touchMode != "touchpad") {
        touchMode = "screen";
    }
    if (remote != "yes" && remote != "no" && remote != "auto") {
        remote = "yes";
    }
    packetSize = std::max(512, std::min(1392, packetSize));
    packetSize -= packetSize % 16;
    int volumeBaseline = getIntPropertyAllowZero(ctx, options, "volumeBaseline", -1);
    if (volumeBaseline > 100) {
        volumeBaseline = 100;
    }

    if (host.empty()) {
        std::fprintf(stderr, "moonlight host is empty\n");
        return -1;
    }

    ensureDirRecursive(workdir);

    if (!runtimePath.empty()) {
        if (!prepareMoonlightRuntime(runtimePath, workdir)) {
            return -1;
        }
        if (binary.empty()) {
            binary = joinPath(workdir, "moonlight-rk3562");
        }
    }
    if (!keyDir.empty() && !ensureDirRecursive(keyDir)) {
        return -1;
    }
    ensureDirRecursive(parentDir(logPath));

    bool otgHostPrepared = false;
    if (otgHost) {
        otgHostPrepared = setRkOtgMode("host");
        if (otgHostPrepared) {
            usleep(1500000);
        }
    }
    applyStreamingKeepAlive();

    std::vector<std::string> args;
    args.push_back(binary);
    args.push_back("stream");
    args.push_back("-platform");
    args.push_back("rk");
    args.push_back("-codec");
    args.push_back("h264");
    args.push_back("-width");
    args.push_back(std::to_string(width));
    args.push_back("-height");
    args.push_back(std::to_string(height));
    args.push_back("-fps");
    args.push_back(std::to_string(fps));
    args.push_back("-bitrate");
    args.push_back(std::to_string(bitrate));
    args.push_back("-packetsize");
    args.push_back(std::to_string(packetSize));
    args.push_back("-remote");
    args.push_back(remote);
    args.push_back("-rotate");
    args.push_back(std::to_string(rotate));
    args.push_back("-app");
    args.push_back(app);
    if (!keyDir.empty()) {
        args.push_back("-keydir");
        args.push_back(keyDir);
    }
    if (viewOnly) {
        args.push_back("-viewonly");
    }
    args.push_back(host);

    pid_t pid = fork();
    if (pid < 0) {
        if (otgHostPrepared) {
            setRkOtgMode("otg");
        }
        return -1;
    }

    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(126);
        }
        setpgid(0, 0);
        chdir(workdir.c_str());

        int logFd = open(logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            if (logFd > STDERR_FILENO) {
                close(logFd);
            }
        }

        std::string libraryPath = workdir + ":" + joinPath(workdir, "libgamestream") + ":/usr/lib:/lib";
        setenv("LD_LIBRARY_PATH", libraryPath.c_str(), 1);
        setenv("MOONLIGHT_RK_RGBFRAME", "0", 1);
        setenv("MOONLIGHT_RK_DRM_STRETCH", stretch ? "1" : "0", 1);
        setenv("MOONLIGHT_RK_DRM_TAKEOVER", takeover ? "1" : "0", 1);
        setenv("MOONLIGHT_RK_DRM_ZPOS", "3", 0);
        setenv("MOONLIGHT_RK_TOUCH_MODE", touchMode.c_str(), 1);
        if (!touchDevice.empty()) {
            setenv("MOONLIGHT_RK_TOUCH_DEVICE", touchDevice.c_str(), 1);
        } else {
            unsetenv("MOONLIGHT_RK_TOUCH_DEVICE");
        }
        const std::string touchRotationEnv = std::to_string(touchRotation);
        const std::string touchOffsetXEnv = std::to_string(touchOffsetX);
        const std::string touchOffsetYEnv = std::to_string(touchOffsetY);
        setenv("MOONLIGHT_RK_TOUCH_ROTATION", touchRotationEnv.c_str(), 1);
        setenv("MOONLIGHT_RK_TOUCH_OFFSET_X", touchOffsetXEnv.c_str(), 1);
        setenv("MOONLIGHT_RK_TOUCH_OFFSET_Y", touchOffsetYEnv.c_str(), 1);
        if (!viewOnly) {
            setenv("MOONLIGHT_RK_EXPECT_GAMEPAD", "1", 1);
        } else {
            unsetenv("MOONLIGHT_RK_EXPECT_GAMEPAD");
        }
        if (volumeBaseline >= 0) {
            std::string volumeBaselineEnv = std::to_string(volumeBaseline);
            setenv("MOONLIGHT_RK_BASE_VOLUME", volumeBaselineEnv.c_str(), 1);
        } else {
            unsetenv("MOONLIGHT_RK_BASE_VOLUME");
        }
        unsetenv("MOONLIGHT_RGBFRAME_SHM");

        std::vector<char *> argv;
        for (std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binary.c_str(), argv.data());
        _exit(127);
    }

    moonlightPid_ = pid;
    otgHostActive_.store(otgHostPrepared);
    writeMoonlightPidFile(pid);
    startScreenKeepAlive();
    return pid;
}

int JSRgbFramePlayer::pairMoonlightProcess(JQFunctionInfo &info)
{
    std::lock_guard<std::mutex> lock(processMutex_);

    JSContext *ctx = info.GetContext();
    JSValueConst options = info.Length() > 0 ? info[0] : JS_UNDEFINED;

    std::string workdir = getStringProperty(ctx, options, "workdir", "/tmp/moonlight-rk3562");
    std::string runtimePath = getStringProperty(ctx, options, "runtimePath", "");
    std::string binary = getStringProperty(ctx, options, "binary", runtimePath.empty() ? "/tmp/moonlight-rk3562/moonlight-rk3562" : "");
    std::string logPath = getStringProperty(ctx, options, "logPath", joinPath(workdir, "moonlight-pair.log"));
    std::string keyDir = getStringProperty(ctx, options, "keyDir", joinPath(workdir, "keys"));
    std::string host = getStringProperty(ctx, options, "host", "");
    std::string pin = getStringProperty(ctx, options, "pin", "");

    if (host.empty()) {
        std::fprintf(stderr, "moonlight pair host is empty\n");
        return -1;
    }
    if (pin.empty()) {
        std::fprintf(stderr, "moonlight pair pin is empty\n");
        return -1;
    }

    if (!runtimePath.empty()) {
        if (!prepareMoonlightRuntime(runtimePath, workdir)) {
            return -1;
        }
        if (binary.empty()) {
            binary = joinPath(workdir, "moonlight-rk3562");
        }
    }
    if (!keyDir.empty() && !ensureDirRecursive(keyDir)) {
        return -1;
    }
    ensureDirRecursive(parentDir(logPath));

    std::vector<std::string> args;
    args.push_back(binary);
    args.push_back("pair");
    args.push_back("-pin");
    args.push_back(pin);
    if (!keyDir.empty()) {
        args.push_back("-keydir");
        args.push_back(keyDir);
    }
    args.push_back(host);

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) {
            _exit(126);
        }
        setpgid(0, 0);
        chdir(workdir.c_str());

        int logFd = open(logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            if (logFd > STDERR_FILENO) {
                close(logFd);
            }
        }

        std::string libraryPath = workdir + ":" + joinPath(workdir, "libgamestream") + ":/usr/lib:/lib";
        setenv("LD_LIBRARY_PATH", libraryPath.c_str(), 1);

        std::vector<char *> argv;
        for (std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binary.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    for (int i = 0; i < 1200; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
            }
            return -1;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        usleep(100000);
    }

    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return -2;
        }
        usleep(100000);
    }
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -2;
}

void JSRgbFramePlayer::stopMoonlightProcess()
{
    stopScreenKeepAlive();

    std::lock_guard<std::mutex> lock(processMutex_);

    pid_t pid = moonlightPid_ > 1 ? moonlightPid_ : readMoonlightPidFile();
    if (pid <= 1) {
        unlink(MOONLIGHT_PID_FILE);
        moonlightPid_ = -1;
        restoreOtgIfNeeded();
        return;
    }

    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);

    for (int i = 0; i < 20; ++i) {
        int status = 0;
        if (moonlightPid_ == pid && waitpid(pid, &status, WNOHANG) == pid) {
            moonlightPid_ = -1;
            unlink(MOONLIGHT_PID_FILE);
            restoreOtgIfNeeded();
            return;
        }
        if (!processExists(pid)) {
            moonlightPid_ = -1;
            unlink(MOONLIGHT_PID_FILE);
            restoreOtgIfNeeded();
            return;
        }
        usleep(100000);
    }

    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    if (moonlightPid_ == pid) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
    moonlightPid_ = -1;
    unlink(MOONLIGHT_PID_FILE);
    restoreOtgIfNeeded();
}

void JSRgbFramePlayer::restoreOtgIfNeeded()
{
    if (otgHostActive_.exchange(false)) {
        setRkOtgMode("otg");
    }
}

void JSRgbFramePlayer::startScreenKeepAlive()
{
    bool expected = false;
    if (!screenKeepAliveRunning_.compare_exchange_strong(expected, true)) {
        return;
    }

    screenKeepAliveThread_ = std::thread(&JSRgbFramePlayer::screenKeepAliveLoop, this);
}

void JSRgbFramePlayer::stopScreenKeepAlive()
{
    if (!screenKeepAliveRunning_.exchange(false)) {
        return;
    }

    screenKeepAliveCv_.notify_all();
    if (screenKeepAliveThread_.joinable()) {
        screenKeepAliveThread_.join();
    }
}

void JSRgbFramePlayer::screenKeepAliveLoop()
{
    while (screenKeepAliveRunning_.load()) {
        if (otgHostActive_.load()) {
            setRkOtgMode("host");
        }
        applyStreamingKeepAlive();

        pid_t pid = readMoonlightPidFile();
        if (pid <= 1 || !processExists(pid)) {
            screenKeepAliveRunning_.store(false);
            break;
        }

        std::unique_lock<std::mutex> lock(screenKeepAliveMutex_);
        screenKeepAliveCv_.wait_for(lock, std::chrono::seconds(5), [this] {
            return !screenKeepAliveRunning_.load();
        });
    }
}

bool JSRgbFramePlayer::isMoonlightProcessRunning()
{
    std::lock_guard<std::mutex> lock(processMutex_);

    pid_t pid = moonlightPid_ > 1 ? moonlightPid_ : readMoonlightPidFile();
    if (pid <= 1) {
        return false;
    }

    if (moonlightPid_ == pid) {
        int status = 0;
        pid_t waitResult = waitpid(pid, &status, WNOHANG);
        if (waitResult == pid) {
            moonlightPid_ = -1;
            unlink(MOONLIGHT_PID_FILE);
            return false;
        }
    }

    return processExists(pid);
}

void JSRgbFramePlayer::startWorker()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    worker_ = std::thread(&JSRgbFramePlayer::workerLoop, this);
}

void JSRgbFramePlayer::stopWorker()
{
    if (!running_.exchange(false)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    unmapSharedFrame();
}

void JSRgbFramePlayer::workerLoop()
{
    uint32_t frameNo = 0;

    while (running_.load()) {
        auto started = std::chrono::steady_clock::now();
        int interval = 33;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            interval = frameIntervalMs_;
        }

        if (!copySharedFrame()) {
            drawGradientFrame(frameNo++);
        }
        onDrawFrame.emit();

        std::this_thread::sleep_until(started + std::chrono::milliseconds(interval));
    }
}

bool JSRgbFramePlayer::mapSharedFrame()
{
    const char *path = std::getenv("MOONLIGHT_RGBFRAME_SHM");
    if (!path || path[0] == '\0') {
        path = "/tmp/moonlight-rgbframe.shm";
    }

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(SharedFrameHeader))) {
        return false;
    }

    if (sharedFrameBase_ && sharedFrameSize_ == static_cast<size_t>(st.st_size)) {
        return true;
    }

    unmapSharedFrame();

    sharedFrameFd_ = open(path, O_RDONLY | O_CLOEXEC);
    if (sharedFrameFd_ < 0) {
        return false;
    }

    sharedFrameSize_ = static_cast<size_t>(st.st_size);
    sharedFrameBase_ = static_cast<uint8_t *>(mmap(nullptr, sharedFrameSize_, PROT_READ, MAP_SHARED, sharedFrameFd_, 0));
    if (sharedFrameBase_ == MAP_FAILED) {
        sharedFrameBase_ = nullptr;
        unmapSharedFrame();
        return false;
    }

    lastSharedFrameNo_ = 0;
    return true;
}

void JSRgbFramePlayer::unmapSharedFrame()
{
    if (sharedFrameBase_) {
        munmap(sharedFrameBase_, sharedFrameSize_);
    }
    if (sharedFrameFd_ >= 0) {
        close(sharedFrameFd_);
    }

    sharedFrameBase_ = nullptr;
    sharedFrameSize_ = 0;
    sharedFrameFd_ = -1;
    lastSharedFrameNo_ = 0;
}

bool JSRgbFramePlayer::copySharedFrame()
{
    if (!mapSharedFrame()) {
        return false;
    }

    const auto *header = reinterpret_cast<const SharedFrameHeader *>(sharedFrameBase_);
    if (header->magic != RGBFRAME_MAGIC || header->version != RGBFRAME_VERSION ||
        header->width == 0 || header->height == 0 ||
        header->stride < header->width * 4 ||
        (header->format != RGBFRAME_FORMAT_RGBA8888 && header->format != RGBFRAME_FORMAT_BGRA8888)) {
        unmapSharedFrame();
        return false;
    }

    const size_t payloadSize = static_cast<size_t>(header->stride) * header->height;
    const size_t requiredSize = sizeof(SharedFrameHeader) + payloadSize;
    if (static_cast<size_t>(header->dataSize) < payloadSize || sharedFrameSize_ < requiredSize) {
        unmapSharedFrame();
        return false;
    }

    const uint32_t frameNo = header->frameNo;
    if (frameNo == 0) {
        return true;
    }
    if (frameNo == lastSharedFrameNo_) {
        return true;
    }

    uint8_t *dst = nullptr;
    size_t dstSize = 0;
    int dstWidth = 0;
    int dstHeight = 0;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        dst = imageData_;
        dstSize = imageDataSize_;
        dstWidth = width_;
        dstHeight = height_;
    }

    if (!dst || dstWidth <= 0 || dstHeight <= 0 ||
        dstSize < static_cast<size_t>(dstWidth) * dstHeight * 4) {
        return false;
    }

    const auto *src = sharedFrameBase_ + sizeof(SharedFrameHeader);
    const uint32_t srcWidth = header->width;
    const uint32_t srcHeight = header->height;
    const uint32_t srcStride = header->stride;

    if (srcWidth == static_cast<uint32_t>(dstWidth) &&
        srcHeight == static_cast<uint32_t>(dstHeight) &&
        header->format == RGBFRAME_FORMAT_RGBA8888) {
        std::memcpy(dst, src, static_cast<size_t>(dstWidth) * dstHeight * 4);
    } else {
        for (int y = 0; y < dstHeight; ++y) {
            const uint32_t sy = (static_cast<uint64_t>(y) * srcHeight) / static_cast<uint32_t>(dstHeight);
            const uint8_t *srcRow = src + static_cast<size_t>(sy) * srcStride;
            uint8_t *dstRow = dst + static_cast<size_t>(y) * dstWidth * 4;

            for (int x = 0; x < dstWidth; ++x) {
                const uint32_t sx = (static_cast<uint64_t>(x) * srcWidth) / static_cast<uint32_t>(dstWidth);
                const uint8_t *srcPx = srcRow + sx * 4;
                uint8_t *dstPx = dstRow + x * 4;

                if (header->format == RGBFRAME_FORMAT_BGRA8888) {
                    dstPx[0] = srcPx[2];
                    dstPx[1] = srcPx[1];
                    dstPx[2] = srcPx[0];
                    dstPx[3] = srcPx[3];
                } else {
                    dstPx[0] = srcPx[0];
                    dstPx[1] = srcPx[1];
                    dstPx[2] = srcPx[2];
                    dstPx[3] = srcPx[3];
                }
            }
        }
    }

    lastSharedFrameNo_ = frameNo;
    return true;
}

void JSRgbFramePlayer::drawGradientFrame(uint32_t frameNo)
{
    uint8_t *buffer = nullptr;
    size_t size = 0;
    int width = 0;
    int height = 0;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        buffer = imageData_;
        size = imageDataSize_;
        width = width_;
        height = height_;
    }

    if (!buffer || width <= 0 || height <= 0 || size < static_cast<size_t>(width) * height * 4) {
        return;
    }

    const int phase = static_cast<int>(frameNo % 768);
    const int stride = width * 4;

    for (int y = 0; y < height; ++y) {
        uint8_t *row = buffer + static_cast<size_t>(y) * stride;
        const int gy = height > 1 ? (y * 255) / (height - 1) : 0;
        for (int x = 0; x < width; ++x) {
            const int gx = width > 1 ? (x * 255) / (width - 1) : 0;
            const int wave = (phase + gx + gy) % 768;

            int r;
            int g;
            int b;
            if (wave < 256) {
                r = 255 - wave;
                g = wave;
                b = gx;
            } else if (wave < 512) {
                r = gy;
                g = 511 - wave;
                b = wave - 256;
            } else {
                r = wave - 512;
                g = gx;
                b = 767 - wave;
            }

            uint8_t *px = row + x * 4;
            px[0] = clampByte(r);
            px[1] = clampByte(g);
            px[2] = clampByte(b);
            px[3] = 0xff;
        }
    }
}

static JSValue createRgbFramePlayer(JQModuleEnv *env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "rgbFramePlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        return new JSRgbFramePlayer();
    });
    tpl->SetProtoMethod("setVideoInfo", &JSRgbFramePlayer::setVideoInfo);
    tpl->SetProtoMethod("setImageDataBuffer", &JSRgbFramePlayer::setImageDataBuffer);
    tpl->SetProtoMethod("setFrameInterval", &JSRgbFramePlayer::setFrameInterval);
    tpl->SetProtoMethod("start", &JSRgbFramePlayer::start);
    tpl->SetProtoMethod("stop", &JSRgbFramePlayer::stop);
    tpl->SetProtoMethod("isRunning", &JSRgbFramePlayer::isRunning);
    tpl->SetProtoMethod("startMoonlight", &JSRgbFramePlayer::startMoonlight);
    tpl->SetProtoMethod("stopMoonlight", &JSRgbFramePlayer::stopMoonlight);
    tpl->SetProtoMethod("isMoonlightRunning", &JSRgbFramePlayer::isMoonlightRunning);
    tpl->SetProtoMethod("pairMoonlight", &JSRgbFramePlayer::pairMoonlight);
    tpl->SetProtoMethod("getDrmScreenSize", &JSRgbFramePlayer::getDrmScreenSize);
    tpl->SetProtoMethod("getSystemDisplayConfig", &JSRgbFramePlayer::getSystemDisplayConfig);
    tpl->InstanceTemplate()->Set("onDrawFrame", &JSRgbFramePlayer::onDrawFrame);
    return tpl->CallConstructor();
}

int rgbframe_init(JQModuleEnv *env)
{
    env->setModuleExport("rgbFramePlayer", createRgbFramePlayer(env));
    return 0;
}

}  // namespace rgbframe
