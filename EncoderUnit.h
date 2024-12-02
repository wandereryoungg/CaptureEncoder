//
// Created by Charlie on 2024/7/24.
//

#ifndef CAPTUREENCODER_ENCODERUNIT_H
#define CAPTUREENCODER_ENCODERUNIT_H

#include <jni.h>
#include <utils/Thread.h>

#include "IProcessDoneListener.h"
#include "IProcessUnit.h"
#include "media/NdkMediaCodec.h"

class EncoderUnit : public IProcessUnit {
public:
    EncoderUnit(IProcessDoneListener* processDoneListener, JavaVM* Jvm, jobject javaEncoder, int preViewFps);

    ~EncoderUnit();

    bool shouldProcessImg() override;

    int32_t processBuffer(
        std::shared_ptr<ProcessBuf>& processBuf) override;

    void waitForNextRequest(std::shared_ptr<ProcessBuf>* out) override;

    AMediaCodec* mCodec;
private:

    class SendResultThread : public Thread {
    public:
        explicit SendResultThread(AMediaCodec* codec, JavaVM* Jvm, jobject javaEncoder);

        virtual ~SendResultThread();

    private:
        virtual bool threadLoop();

        virtual status_t readyToRun();

        AMediaCodec* mCodec;

        JavaVM* globalJvm = nullptr;

        JNIEnv* mJniEnv;

        jobject mJavaEncoder;

        jmethodID mGetVideoMethodId;
    };

    sp<SendResultThread> mSendResultThread = nullptr;

    IProcessDoneListener* mIProcessDoneListener;

    int setupCodec();

    virtual bool threadLoop();

    virtual status_t readyToRun();

    JavaVM* globalJvm = nullptr;

    JNIEnv* mJniEnv;

    jobject mJavaEncoder;

    int mWidth;

    int mHeight;

    int mFps;

    AMediaFormat* mFormat;

    int mSkipCodecNum = 0;

    int mCount = 0;

    int mPts = 0;
};

#endif  // CAPTUREENCODER_ENCODERUNIT_H
