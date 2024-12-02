#define LOG_TAG "JniCaptureModel"

#include <android/native_window_jni.h>
#include <jni.h>
#include <log/log.h>
#include <utils/RefBase.h>

#include <map>
#include <string>

#include "CaptureModel.h"

std::map<jint, sp<CaptureModel>> mCaptureModels;
std::mutex mModelLock;
JavaVM* globalJvm;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    ALOGI("%s", __func__);
    globalJvm = vm;
    JNIEnv* env = NULL;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6)) {
        ALOGE("%s   JNI_OnLoad 1.6 error", __func__);
        return JNI_ERR;
    }
    ALOGI("%s   success", __func__);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_vhd_captureencoder_CaptureModel_nativeInit(JNIEnv* env, jobject thiz,
                                                    jint camera_id) {
    std::lock_guard<std::mutex> lk(mModelLock);
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    if (mCaptureModels.find(camera_id) == mCaptureModels.end()) {
        sp<CaptureModel> captureModel = new CaptureModel(camera_id, globalJvm);
        mCaptureModels[camera_id] = captureModel;
    } else {
        ALOGE("%s   already get captureModel for camera: %d", __func__,
              camera_id);
    }
    for (const auto& pair : mCaptureModels) {
        ALOGI("%s   camera_id: %d captureModel: %p", __func__, pair.first,
              pair.second.get());
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_vhd_captureencoder_CaptureModel_nativeRelease(JNIEnv* env,
                                                       jobject thiz,
                                                       jint camera_id) {
    std::lock_guard<std::mutex> lk(mModelLock);
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    auto iter = mCaptureModels.find(camera_id);
    if (iter != mCaptureModels.end()) {
        mCaptureModels.erase(camera_id);
    }
    for (const auto& pair : mCaptureModels) {
        ALOGI("%s   camera_id: %d captureModel: %p", __func__, pair.first,
              pair.second.get());
    }
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_vhd_captureencoder_CaptureModel_startCapture(JNIEnv* env, jobject thiz,
                                                      jint camera_id,
                                                      jobject surface,
                                                      jint width, jint height,
                                                      jint fps) {
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    std::lock_guard<std::mutex> lk(mModelLock);
    auto iter = mCaptureModels.find(camera_id);
    if (iter != mCaptureModels.end()) {
        sp<CaptureModel> captureModel = iter->second;
        captureModel->stopCapture();
        ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
        if (nativeWindow) {
            return captureModel->startCapture(nativeWindow, width, height, fps);
        } else {
            ALOGE("%s   nativeWindow is null", __func__);
            return -1;
        }
    } else {
        ALOGI("%s   camera_id: %d not init", __func__, camera_id);
        return -1;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_vhd_captureencoder_CaptureModel_stopCapture(JNIEnv* env, jobject thiz,
                                                     jint camera_id) {
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    std::lock_guard<std::mutex> lk(mModelLock);
    auto iter = mCaptureModels.find(camera_id);
    if (iter != mCaptureModels.end()) {
        sp<CaptureModel> captureModel = iter->second;
        return captureModel->stopCapture();
    } else {
        ALOGI("%s   camera_id: %d not init", __func__, camera_id);
        return -1;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_vhd_captureencoder_CaptureModel_addEncoder(JNIEnv* env, jobject thiz,
                                                    jint camera_id,
                                                    jobject encoder_model) {
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    std::lock_guard<std::mutex> lk(mModelLock);
    auto iter = mCaptureModels.find(camera_id);
    if (iter != mCaptureModels.end()) {
        sp<CaptureModel> captureModel = iter->second;
        jobject encoderModel = env->NewGlobalRef(encoder_model);
        return captureModel->addEncoderUnit(encoderModel);
    } else {
        ALOGI("%s   camera_id: %d not init", __func__, camera_id);
        return -1;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_vhd_captureencoder_CaptureModel_removeEncoder(JNIEnv* env,
                                                       jobject thiz,
                                                       jint camera_id,
                                                       jint encoder_id) {
    ALOGI("%s   camera_id: %d", __func__, camera_id);
    std::lock_guard<std::mutex> lk(mModelLock);
    auto iter = mCaptureModels.find(camera_id);
    if (iter != mCaptureModels.end()) {
        sp<CaptureModel> captureModel = iter->second;
        jobject javaObj = captureModel->removeEncoderUnit(encoder_id);
        if (javaObj) {
            env->DeleteGlobalRef(javaObj);
        } else {
            ALOGE("%s   camera_id: %d javaObj is null", __func__, camera_id);
            return -1;
        }
        return 0;
    } else {
        ALOGI("%s   camera_id: %d not init", __func__, camera_id);
        return -1;
    }
}