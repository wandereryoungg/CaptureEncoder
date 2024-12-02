//
// Created by Charlie on 2024/7/24.
//
#define LOG_TAG "NativeEncoderUnit"

#include "EncoderUnit.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "RgaCropScale.h"
#include "JNIEnvUtil.h"

constexpr int COLOR_FormatYUV420Flexible = 0x7F420888;
#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15

static const int64_t TIMEOUT_USEC = 12000;

EncoderUnit::EncoderUnit(IProcessDoneListener* processDoneListener, JavaVM *Jvm, jobject javaEncoder, int preViewFps)
    : mIProcessDoneListener(processDoneListener), globalJvm(Jvm),
      mJavaEncoder(javaEncoder) {
    mJniEnv = getJniEnv(globalJvm);
    if (mJniEnv) {
        jclass clazz = mJniEnv->GetObjectClass(mJavaEncoder);

        jfieldID widthField = mJniEnv->GetFieldID(clazz, "mWidth", "I");
        if (widthField == nullptr) {
            ALOGE("%s   cannot get widthField errno: %s", __func__, strerror(errno));
        } else {
            mWidth = mJniEnv->GetIntField(mJavaEncoder, widthField);
        }

        jfieldID heightField = mJniEnv->GetFieldID(clazz, "mHeight", "I");
        if (heightField == nullptr) {
            ALOGE("%s   cannot get heightField errno: %s", __func__, strerror(errno));
        } else {
            mHeight = mJniEnv->GetIntField(mJavaEncoder, heightField);
        }

        jfieldID fpsField = mJniEnv->GetFieldID(clazz, "mFps", "I");
        if (fpsField == nullptr) {
            ALOGE("%s   cannot get fpsField errno: %s", __func__, strerror(errno));
        } else {
            mFps = mJniEnv->GetIntField(mJavaEncoder, fpsField);
            mSkipCodecNum = preViewFps / mFps;
        }

    } else {
        ALOGE("%s   cannot get jni env errno: %s", __func__, strerror(errno));
    }

    ALOGI("%s   EncoderUnit: %p width: %d height: %d fps: %d mSkipCodecNum: %d", __func__, this,
          mWidth, mHeight, mFps, mSkipCodecNum);
}

EncoderUnit::~EncoderUnit() {
    ALOGI("%s   EncoderUnit: %p", __func__, this);

    if (mSendResultThread) {
        mSendResultThread->requestExit();
        mSendResultThread->join();
        mSendResultThread.clear();
        mSendResultThread = nullptr;
    }

    if (mCodec) {
        AMediaCodec_delete(mCodec);
    }

    if (mFormat) {
        AMediaFormat_delete(mFormat);
    }
}

bool EncoderUnit::shouldProcessImg() {
    return true;
}

int32_t EncoderUnit::processBuffer(
    std::shared_ptr<ProcessBuf> &processBuf) {
    ATRACE_CALL();
    mCount++;
    std::unique_lock<std::mutex> lk(mProcessLock);
    mProcessList.push_back(processBuf);
    lk.unlock();
    mProcessCond.notify_one();
    return 0;
}

status_t EncoderUnit::readyToRun() {
    ALOGI("%s   EncoderUnit: %p", __func__, this);
    int status = setupCodec();
    if (status) {
        return -errno;
    }

    mSendResultThread = new SendResultThread(mCodec, globalJvm, mJavaEncoder);
    mSendResultThread->run("SendResultThread");

    return NO_ERROR;
}

int EncoderUnit::setupCodec() {
    mCodec = AMediaCodec_createCodecByName("c2.rk.hevc.encoder");
    if (!mCodec) {
        ALOGE("%s   unable to create codec", __func__);
        return -errno;
    }
    mFormat = AMediaFormat_new();
    AMediaFormat_setString(mFormat, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    AMediaFormat_setInt32(mFormat, AMEDIAFORMAT_KEY_BIT_RATE, 1920 * 1000);
    AMediaFormat_setInt32(mFormat, AMEDIAFORMAT_KEY_WIDTH, mWidth);
    AMediaFormat_setInt32(mFormat, AMEDIAFORMAT_KEY_HEIGHT, mHeight);
    AMediaFormat_setInt32(mFormat, AMEDIAFORMAT_KEY_FRAME_RATE, mFps);
    AMediaFormat_setFloat(mFormat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 5.0F);
    AMediaFormat_setInt32(mFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          COLOR_FormatYUV420Flexible);
    ALOGI("%s   codec mFormat: %s", __func__, AMediaFormat_toString(mFormat));

    media_status_t status = AMediaCodec_configure(
        mCodec, mFormat, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status) {
        ALOGE("%s   unable to config codec: %s", __func__, strerror(errno));
        return -errno;
    }
    status = AMediaCodec_start(mCodec);
    if (status) {
        ALOGE("%s   unable to start codec: %s", __func__, strerror(errno));
        return -errno;
    }
    ALOGI("%s   success", __func__);
    return 0;
}

void EncoderUnit::waitForNextRequest(std::shared_ptr<ProcessBuf> *out) {
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

bool EncoderUnit::threadLoop() {

    std::shared_ptr<ProcessBuf> processBuf;
    waitForNextRequest(&processBuf);
    if (processBuf == nullptr) {
        return true;
    }
    // input buffer
    ssize_t bufIndex = AMediaCodec_dequeueInputBuffer(mCodec, TIMEOUT_USEC);
    ALOGD("AMediaCodec_dequeueInputBuffer index: %zd", bufIndex);
    if (bufIndex >= 0) {
        {
            std::unique_lock<std::mutex> lk(mProcessLock);
            mProcessList.pop_front();
        }
        size_t bufsize;
        uint64_t pts = mPts * 1000000 / mFps;
        uint8_t *dstBuf = AMediaCodec_getInputBuffer(mCodec, bufIndex, &bufsize);
        int format = HAL_PIXEL_FORMAT_YCrCb_NV12;
        if (processBuf->format == V4L2_PIX_FMT_YUYV) {
            format = 0x1c << 8;
        }
        RgaCropScale::convertFormat(processBuf->width, processBuf->height, -1,
                                    processBuf->start, format, mWidth, mHeight,
                                    -1, dstBuf, HAL_PIXEL_FORMAT_YCrCb_NV12);
        ALOGI("%s   processBuf index: %d processNum: %d", __func__, processBuf->index, processBuf->processNum);
        if (mIProcessDoneListener) {
            mIProcessDoneListener->notifyProcessDone(processBuf);
        }
        ALOGI("%s   AMediaCodec_queueInputBuffer pts: %llu", __func__, pts);
        // 入队列
        AMediaCodec_queueInputBuffer(mCodec, bufIndex, 0, mWidth * mHeight * 1.5, pts, 0);
        mPts++;
    }

    return true;
}

EncoderUnit::SendResultThread::SendResultThread(AMediaCodec* codec, JavaVM* Jvm, jobject javaEncoder)
    : mCodec(codec),
      globalJvm(Jvm),
      mJavaEncoder(javaEncoder) {
    ALOGI("%s   SendResultThread: %p", __func__, this);
}

EncoderUnit::SendResultThread::~SendResultThread() {
    ALOGI("%s   SendResultThread: %p", __func__, this);
}

status_t EncoderUnit::SendResultThread::readyToRun() {
    ALOGI("%s   SendResultThread: %p", __func__, this);
    mJniEnv = getJniEnv(globalJvm);
    if (mJniEnv) {
        jclass clazz = mJniEnv->GetObjectClass(mJavaEncoder);
        mGetVideoMethodId =
            mJniEnv->GetMethodID(clazz, "onGetVideoFrame", "([BI)V");
        ALOGI("%s   mGetVideoMethodId: %p", __func__, mGetVideoMethodId);
    } else {
        ALOGE("%s   cannot get jni env errno: %s", __func__, strerror(errno));
        return -errno;
    }
    return NO_ERROR;
}

bool EncoderUnit::SendResultThread::threadLoop() {
    AMediaCodecBufferInfo info;
    // output buffer
    auto outIndex = AMediaCodec_dequeueOutputBuffer(mCodec, &info, TIMEOUT_USEC);
    ALOGD("AMediaCodec_dequeueOutputBuffer outIndex: %zd", outIndex);
    if (outIndex >= 0) {
        size_t outsize;
        uint8_t *buf = AMediaCodec_getOutputBuffer(mCodec, outIndex, &outsize);
        if (mJniEnv && mJavaEncoder && mGetVideoMethodId) {
            jbyteArray array = mJniEnv->NewByteArray(info.size);
            mJniEnv->SetByteArrayRegion(array, 0, info.size,
                                        reinterpret_cast<const jbyte *>(buf));
            mJniEnv->CallVoidMethod(mJavaEncoder, mGetVideoMethodId, array,
                                    info.size);
            mJniEnv->DeleteLocalRef(array);
        }
        AMediaCodec_releaseOutputBuffer(mCodec, outIndex, false);
    }
    return true;
}
