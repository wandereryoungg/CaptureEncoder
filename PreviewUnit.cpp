//
// Created by Charlie on 2024/7/24.
//
#define LOG_TAG "NativePreviewUnit"

#include "PreviewUnit.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "RgaCropScale.h"

#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15

PreviewUnit::PreviewUnit(IProcessDoneListener* processDoneListener, ANativeWindow* nativeWindow, int width, int height)
    : mIProcessDoneListener(processDoneListener), mNativeWindow(nativeWindow), mWidth(width), mHeight(height) {
    ALOGI("%s   PreviewUnit: %p nativeWindow: %p width: %d height: %d",
          __func__, this, nativeWindow, width, height);
    if (mNativeWindow) {
        ANativeWindow_setBuffersGeometry(mNativeWindow, mWidth, mHeight,
                                         HAL_PIXEL_FORMAT_YCrCb_NV12);
    }
}

PreviewUnit::~PreviewUnit() {
    ALOGI("%s   PreviewUnit: %p", __func__, this);
    if (mNativeWindow) {
        ANativeWindow_release(mNativeWindow);
        mNativeWindow = nullptr;
    }
}

status_t PreviewUnit::readyToRun() {
    ALOGI("%s   PreviewUnit: %p", __func__, this);
    return NO_ERROR;
}

int32_t PreviewUnit::processBuffer(
    std::shared_ptr<ProcessBuf>& processBuf) {
    ATRACE_CALL();
    std::unique_lock<std::mutex> lk(mProcessLock);
    mProcessList.push_back(processBuf);
    lk.unlock();
    mProcessCond.notify_one();
    return 0;
}

void PreviewUnit::waitForNextRequest(std::shared_ptr<ProcessBuf>* out) {
    ATRACE_CALL();
    if (out == nullptr) {
        ALOGE("%s   out is null", __func__);
        return;
    }

    std::unique_lock<std::mutex> lk(mProcessLock);
    int waitTimes = 0;
    while (mProcessList.empty()) {
        if (exitPending()) {
            return;
        }
        std::chrono::milliseconds timeout =
            std::chrono::milliseconds(kReqWaitTimeoutMs);
        auto st = mProcessCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            waitTimes++;
            if (waitTimes == kReqWaitTimesMax) {
                // no new request, return
                return;
            }
        }
    }
    *out = mProcessList.front();
}

bool PreviewUnit::threadLoop() {
    std::shared_ptr<ProcessBuf> processBuf;
    waitForNextRequest(&processBuf);
    if (processBuf == nullptr) {
        return true;
    }
    if (mNativeWindow) {
        ANativeWindow_Buffer windowBuffer;
        if (ANativeWindow_lock(mNativeWindow, &windowBuffer, NULL) == 0) {
            {
                std::unique_lock<std::mutex> lk(mProcessLock);
                mProcessList.pop_front();
            }
            int format = HAL_PIXEL_FORMAT_YCrCb_NV12;
            if (processBuf->format == V4L2_PIX_FMT_YUYV) {
                format = 0x1c << 8;
            }
            uint8_t* dstBuf = (uint8_t*)windowBuffer.bits;
            RgaCropScale::convertFormat(processBuf->width, processBuf->height,
                                        -1, processBuf->start, format, mWidth,
                                        mHeight, -1, dstBuf,
                                        HAL_PIXEL_FORMAT_YCrCb_NV12);
            ALOGI("%s   processBuf index: %d processNum: %d", __func__, processBuf->index, processBuf->processNum);
            if (mIProcessDoneListener) {
                mIProcessDoneListener->notifyProcessDone(processBuf);
            }
            ANativeWindow_unlockAndPost(mNativeWindow);
        } else {
            ALOGE("%s   lock failed: %s", __func__, strerror(errno));
            return -errno;
        }
    } else {
        ALOGE("%s   mNativeWindow is null", __func__);
    }
    return true;
}
