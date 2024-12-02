//
// Created by Charlie on 2024/7/31.
//
#define LOG_TAG "NativeMppEncoderUnit"

#include "MppEncoderUnit.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "RgaCropScale.h"
#include "JNIEnvUtil.h"

constexpr int COLOR_FormatYUV420Flexible = 0x7F420888;
#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15

static const int64_t TIMEOUT_USEC = 12000;

MppEncoderUnit::MppEncoderUnit(IProcessDoneListener* processDoneListener, v4l2Buffer* buffer, JavaVM *Jvm, jobject javaEncoder, int preViewFps)
    : mIProcessDoneListener(processDoneListener), mv4l2Buffer(buffer), globalJvm(Jvm),
      mJavaEncoder(javaEncoder), mPreviewFps(preViewFps) {
    mEncodeData = (unsigned char*)malloc(2 * 1024 * 1024);
    ALOGI("%s   mEncodeData: %p", __func__, mEncodeData);
}

MppEncoderUnit::~MppEncoderUnit() {
    ALOGI("%s   MppEncoderUnit: %p", __func__, this);
    if (mEncodeData) {
        free(mEncodeData);
        mEncodeData = nullptr;
    }
    /*
    if (mSendResultThread) {
        mSendResultThread->requestExit();
        mSendResultThread->join();
        mSendResultThread.clear();
        mSendResultThread = nullptr;
    }
    */
}

bool MppEncoderUnit::shouldProcessImg() {
    return true;
}

int32_t MppEncoderUnit::processBuffer(
    std::shared_ptr<ProcessBuf> &processBuf) {
    ATRACE_CALL();
    std::unique_lock<std::mutex> lk(mProcessLock);
    mProcessList.push_back(processBuf);
    lk.unlock();
    mProcessCond.notify_one();
    return 0;
}

status_t MppEncoderUnit::readyToRun() {
    ALOGI("%s   MppEncoderUnit: %p", __func__, this);
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
            mSkipCodecNum = mPreviewFps / mFps;
        }

        mGetVideoMethodId =
            mJniEnv->GetMethodID(clazz, "onGetVideoFrame", "([BI)V");
        ALOGI("%s   mGetVideoMethodId: %p", __func__, mGetVideoMethodId);


    } else {
        ALOGE("%s   cannot get jni env errno: %s", __func__, strerror(errno));
        return -1;
    }

    ALOGI("%s   MppEncoderUnit: %p width: %d height: %d fps: %d mSkipCodecNum: %d", __func__, this,
          mWidth, mHeight, mFps, mSkipCodecNum);
    int status = setupCodec();
    if (status) {
        return -errno;
    }
    /*
    mSendResultThread = new SendResultThread(&mppEncoder, globalJvm, mJavaEncoder);
    mSendResultThread->run("SendResultThread");
    */
    return NO_ERROR;
}

int MppEncoderUnit::setupCodec() {
    ALOGI("%s   success", __func__);
    int channelId = MppChannel::getInstance().getChannel();
    {
        memset(&mCodecParam, 0, sizeof(mCodecParam));
        mCodecParam.format = MPP_FMT_YUV420SP;
        mCodecParam.width = mWidth;
        mCodecParam.height = mHeight;
        mCodecParam.type = MPP_VIDEO_CodingHEVC;
        mCodecParam.rc_mode = MPP_ENC_RC_MODE_FIXQP;
        mCodecParam.rc_mode = MPP_ENC_RC_MODE_FIXQP;
        mCodecParam.bps_target = 1920 * 1000;
        mCodecParam.gop_len = 60;
    }

    int ret = mppEncoder.venc_init(channelId, &mCodecParam, mv4l2Buffer, mWidth * mHeight * 1.5);
    if (ret) {
        ALOGE("%s   failed errno: %s", __func__, strerror(errno));
    }
    return 0;
}

void MppEncoderUnit::waitForNextRequest(std::shared_ptr<ProcessBuf> *out) {
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

bool MppEncoderUnit::threadLoop() {

    std::shared_ptr<ProcessBuf> processBuf;
    waitForNextRequest(&processBuf);
    if (processBuf == nullptr) {
        return true;
    }
    mppEncoder.venc_put_src_imge(processBuf->index);

    unsigned long frameLength = 2 * 1024 * 1024;
    unsigned char* encodeData = mEncodeData;
    ALOGI("%s   mEncodeData: %p", __func__, mEncodeData);
    int ret = mppEncoder.venc_get_frame(encodeData, &frameLength);
    if (ret) {
        ALOGE("%s   venc_get_frame errno: %s", __func__, strerror(errno));
        usleep(100000);
        return true;
    }
    ALOGI("%s   venc_get_frame success frameLength: %lu", __func__, frameLength);
    {
        std::unique_lock<std::mutex> lk(mProcessLock);
        mProcessList.pop_front();
    }
    if (mIProcessDoneListener) {
        mIProcessDoneListener->notifyProcessDone(processBuf);
    }

    if (mJniEnv && mJavaEncoder && mGetVideoMethodId) {
        jbyteArray array = mJniEnv->NewByteArray(frameLength);
        mJniEnv->SetByteArrayRegion(array, 0, frameLength,
                                    reinterpret_cast<const jbyte *>(encodeData));
        mJniEnv->CallVoidMethod(mJavaEncoder, mGetVideoMethodId, array,
                                frameLength);
        mJniEnv->DeleteLocalRef(array);
    }

    return true;
}

MppEncoderUnit::SendResultThread::SendResultThread(MppEncoder* codec, JavaVM* Jvm, jobject javaEncoder)
    : mCodec(codec),
      globalJvm(Jvm),
      mJavaEncoder(javaEncoder) {
    ALOGI("%s   SendResultThread: %p", __func__, this);
}

MppEncoderUnit::SendResultThread::~SendResultThread() {
    ALOGI("%s   SendResultThread: %p", __func__, this);
}

status_t MppEncoderUnit::SendResultThread::readyToRun() {
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

bool MppEncoderUnit::SendResultThread::threadLoop() {

    return true;
}

