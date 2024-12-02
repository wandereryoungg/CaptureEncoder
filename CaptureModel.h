//
// Created by Charlie on 2024/7/22.
//

#ifndef CAPTUREENCODER_CAPTUREMODEL_H
#define CAPTUREENCODER_CAPTUREMODEL_H

#include <errno.h>
#include <jni.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include <system/window.h>
#include <utils/RefBase.h>
#include <utils/Thread.h>

#include <map>
#include <mutex>
#include <vector>

#include "IProcessDoneListener.h"
#include "EncoderUnit.h"
#include "MppEncoderUnit.h"
#include "PreviewUnit.h"
#include "JNIEnvUtil.h"

#define V4L2_BUFFER_COUNT 4

using namespace android;

class CaptureModel : public RefBase, public IProcessDoneListener {
public:
    CaptureModel(int cameraId, JavaVM* Jvm);

    ~CaptureModel();

    int startCapture(ANativeWindow* nativeWindow, int width, int height,
                     int fps);

    int stopCapture();

    int loadFd();

    int addEncoderUnit(jobject& javaEncoder);

    jobject removeEncoderUnit(int encoderId);

    void notifyProcessDone(std::shared_ptr<IProcessUnit::ProcessBuf>& processBuf) override;

    struct v4l2_capability mCapability;

    struct v4l2Buffer gV4l2Buf[V4L2_BUFFER_COUNT];

    enum v4l2_buf_type mBufType;

    std::vector<struct v4l2_plane> mPlanes;

private:

    std::mutex mEncoderLock;

    std::map<int, jobject> mJavaEncoders;

    int mEncoderId = 0;

    struct v4l2Param {
        bool getFormat;

        uint32_t mMinDistance;

        uint32_t mV4l2Format;

        uint32_t mV4l2Width;

        uint32_t mV4l2Height;
    };

    class PollThread : public Thread {
    public:
        explicit PollThread(sp<CaptureModel> captureModel,
                            std::list<sp<IProcessUnit>> processList,
                            uint32_t width, uint32_t height, uint32_t format);

        virtual ~PollThread();

        void returnCameraBuffer(std::shared_ptr<IProcessUnit::ProcessBuf>& processBuf);

    private:
        virtual bool threadLoop();

        virtual status_t readyToRun();

        void showDebugFPS();

        sp<CaptureModel> mCaptureModel = nullptr;

        std::list<sp<IProcessUnit>> mProcessList;

        uint32_t mWidth;

        uint32_t mHeight;

        uint32_t mFormat;

        void postProcess(void* start, int index);

        std::mutex mProcessBufLock;

        int mFrameCount;

        int mLastFrameCount;

        nsecs_t mLastFpsTime;
    };

    sp<PollThread> mPollThread = nullptr;

    int mCameraId;

    int mWidth;

    int mHeight;

    int mFps;

    JavaVM* globalJvm = nullptr;

    v4l2Param mSelectParams;

    std::atomic<int> mFd = -1;

    volatile bool mV4l2Streaming = false;

    std::mutex mCaptureLock;

    std::condition_variable mStopCon;

    int enumDeviceFmt();

    int getDeviceFmt();

    int queryCapability();

    int v4l2StreamOff();

    int v4l2SetFmt();

    int v4l2SetFps();

    int v4l2ReqBuf();

    int v4l2StreamOn();

    void selectCameraBuf();

    std::list<sp<IProcessUnit>> mProcessList;

    int querySensorTimings();

    struct v4l2_subdev_format mSubFormat;

    int getSensorFormat();

    int setSensorFormat();

    int setSensorInterval();

    struct v4l2_format mFormat;

};

#endif  // CAPTUREENCODER_CAPTUREMODEL_H
