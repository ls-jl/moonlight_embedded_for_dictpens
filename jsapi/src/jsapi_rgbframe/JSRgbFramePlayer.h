#pragma once

#include "jqutil_v2/jqutil.h"
#include "jqutil_v2/JQSignal.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <sys/types.h>

namespace rgbframe {

class JSRgbFramePlayer : public JQUTIL_NS::JQBaseObject {
public:
    JSRgbFramePlayer();
    ~JSRgbFramePlayer() override;

    JQUTIL_NS::JQSignal<> onDrawFrame;

    void setVideoInfo(JQUTIL_NS::JQFunctionInfo &info);
    void setImageDataBuffer(JQUTIL_NS::JQFunctionInfo &info);
    void setFrameInterval(JQUTIL_NS::JQFunctionInfo &info);
    void start(JQUTIL_NS::JQFunctionInfo &info);
    void stop(JQUTIL_NS::JQFunctionInfo &info);
    void isRunning(JQUTIL_NS::JQFunctionInfo &info);
    void startMoonlight(JQUTIL_NS::JQFunctionInfo &info);
    void stopMoonlight(JQUTIL_NS::JQFunctionInfo &info);
    void isMoonlightRunning(JQUTIL_NS::JQFunctionInfo &info);
    void pairMoonlight(JQUTIL_NS::JQFunctionInfo &info);
    void getDrmScreenSize(JQUTIL_NS::JQFunctionInfo &info);

protected:
    void OnGCCollect() override;

private:
    void startWorker();
    void stopWorker();
    void workerLoop();
    void drawGradientFrame(uint32_t frameNo);
    bool copySharedFrame();
    bool mapSharedFrame();
    void unmapSharedFrame();
    pid_t startMoonlightProcess(JQUTIL_NS::JQFunctionInfo &info);
    int pairMoonlightProcess(JQUTIL_NS::JQFunctionInfo &info);
    void stopMoonlightProcess();
    bool isMoonlightProcessRunning();
    void startScreenKeepAlive();
    void stopScreenKeepAlive();
    void screenKeepAliveLoop();
    void restoreOtgIfNeeded();

    std::mutex stateMutex_;
    std::mutex processMutex_;
    std::mutex screenKeepAliveMutex_;
    std::condition_variable screenKeepAliveCv_;
    uint8_t *imageData_ = nullptr;
    size_t imageDataSize_ = 0;
    int width_ = 0;
    int height_ = 0;
    int frameIntervalMs_ = 33;
    std::atomic<bool> running_;
    std::thread worker_;
    int sharedFrameFd_ = -1;
    uint8_t *sharedFrameBase_ = nullptr;
    size_t sharedFrameSize_ = 0;
    uint32_t lastSharedFrameNo_ = 0;
    pid_t moonlightPid_ = -1;
    std::atomic<bool> screenKeepAliveRunning_;
    std::thread screenKeepAliveThread_;
    std::atomic<bool> otgHostActive_{false};
};

int rgbframe_init(JQUTIL_NS::JQModuleEnv *env);

}  // namespace rgbframe
