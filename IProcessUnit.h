//
// Created by Charlie on 2024/7/24.
//

#ifndef CAPTUREENCODER_IPROCESSUNIT_H
#define CAPTUREENCODER_IPROCESSUNIT_H

#include <stdint.h>
#include <utils/RefBase.h>

#include <list>
#include <mutex>

using namespace android;

class IProcessUnit : public Thread {
public:
    static const int kReqWaitTimeoutMs = 33;  // 33ms
    static const int kReqWaitTimesMax = 90;   // 33ms * 90 ~= 3 sec

    struct ProcessBuf {
        int index;
        void* start;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t processNum;
    };

    IProcessUnit() {}

    virtual ~IProcessUnit() {}

    virtual bool threadLoop() = 0;

    virtual status_t readyToRun() = 0;

    std::mutex mProcessLock;

    std::condition_variable mProcessCond;

    std::list<std::shared_ptr<ProcessBuf>> mProcessList;

    virtual void waitForNextRequest(std::shared_ptr<ProcessBuf>* out) = 0;

    virtual bool shouldProcessImg() {
        return true;
    }

    virtual int32_t processBuffer(
        std::shared_ptr<ProcessBuf>& processBuf) = 0;
};

#endif  // CAPTUREENCODER_IPROCESSUNIT_H
