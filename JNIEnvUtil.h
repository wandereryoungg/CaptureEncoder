//
// Created by Charlie on 2024/7/31.
//

#ifndef CAPTUREENCODER_JNIENVUTIL_H
#define CAPTUREENCODER_JNIENVUTIL_H

#include <log/log.h>

static JNIEnv* getJniEnv(JavaVM* globalJvm) {
    if (globalJvm == nullptr) {
        ALOGE("%s   globalJvm is nullptr", __func__);
        return nullptr;
    }

    JNIEnv *jniEnv = nullptr;
    int status = globalJvm->GetEnv((void **)&jniEnv, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED || jniEnv == nullptr) {
        status = globalJvm->AttachCurrentThread(&jniEnv, nullptr);
        if (status < 0) {
            jniEnv = nullptr;
        }
    }
    if (jniEnv == nullptr) {
        ALOGE("%s   jniEnv is nullptr", __func__);
    }
    return jniEnv;
}

struct v4l2Buffer {
    int index = 0;
    void* start = nullptr;
    unsigned int offset;
    unsigned int length;
    int exportFd = 0;
};

#endif //CAPTUREENCODER_JNIENVUTIL_H
