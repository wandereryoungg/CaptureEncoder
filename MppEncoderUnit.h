//
// Created by Charlie on 2024/7/31.
//

#ifndef CAPTUREENCODER_MPPENCODERUNIT_H
#define CAPTUREENCODER_MPPENCODERUNIT_H

#include <jni.h>
#include <utils/Thread.h>

#include "IProcessDoneListener.h"
#include "IProcessUnit.h"
#include "media/NdkMediaCodec.h"
#include "JNIEnvUtil.h"
#include "venc/mpi_enc.h"
#include "MppChannel.h"

class MppEncoderUnit : public IProcessUnit {
public:
    MppEncoderUnit(IProcessDoneListener* processDoneListener, v4l2Buffer* buffer, JavaVM* Jvm, jobject javaEncoder, int preViewFps);

    ~MppEncoderUnit();

    bool shouldProcessImg() override;

    int32_t processBuffer(
        std::shared_ptr<ProcessBuf>& processBuf) override;

    void waitForNextRequest(std::shared_ptr<ProcessBuf>* out) override;

private:

    class SendResultThread : public Thread {
    public:
        explicit SendResultThread(MppEncoder* codec, JavaVM* Jvm, jobject javaEncoder);

        virtual ~SendResultThread();

    private:
        virtual bool threadLoop();

        virtual status_t readyToRun();

        MppEncoder* mCodec;

        JavaVM* globalJvm = nullptr;

        JNIEnv* mJniEnv;

        jobject mJavaEncoder;

        jmethodID mGetVideoMethodId;
    };

    sp<SendResultThread> mSendResultThread = nullptr;

    IProcessDoneListener* mIProcessDoneListener;

    v4l2Buffer* mv4l2Buffer;

    int setupCodec();

    virtual bool threadLoop();

    virtual status_t readyToRun();

    JavaVM* globalJvm = nullptr;

    JNIEnv* mJniEnv;

    jobject mJavaEncoder;

    int mPreviewFps;

    VENC_ATTR_t mCodecParam;

    MppEncoder mppEncoder;

    unsigned char* mEncodeData = nullptr;

    int mWidth;

    int mHeight;

    int mFps;

    jmethodID mGetVideoMethodId;

    int mSkipCodecNum = 0;

};



#endif //CAPTUREENCODER_MPPENCODERUNIT_H
