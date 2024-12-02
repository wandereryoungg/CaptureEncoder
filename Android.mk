LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    CaptureEncoderJni.cpp \
    CaptureModel.cpp \
    EncoderUnit.cpp \
    MppEncoderUnit.cpp \
    RgaCropScale.cpp \
    PreviewUnit.cpp \
    MppChannel.cpp \
    venc/mpi_enc.cpp \
    venc/mpp/utils/mpi_enc_utils.c \
    venc/mpp/utils/mpp_enc_roi_utils.c \
    venc/mpp/utils/utils.c \
    venc/mpp/utils/iniparser.c \
    venc/mpp/utils/mpp_opt.c \
    venc/mpp/utils/dictionary.c \
    venc/mpp/utils/camera_source.c \
    venc/mpp/utils/mpi_dec_utils.c

LOCAL_MODULE := CaptureEncoder

LOCAL_MULTILIB := 64

LOCAL_HEADER_LIBRARIES += \
    jni_headers

LOCAL_CFLAGS += -Wno-format-security -Wno-unused-parameter -Wno-pointer-arith -Wno-address-of-packed-member

LOCAL_C_INCLUDES += \
    hardware/rockchip/librga \
    frameworks/av/media/ndk/include \
    frameworks/native/libs/nativewindow/include \
    frameworks/base/native/android/include \
    $(LOCAL_PATH)/venc/mpp/inc \
    $(LOCAL_PATH)/venc/mpp/osal/include \
    $(LOCAL_PATH)/venc/mpp/utils \
    $(LOCAL_PATH)/venc/mpp/base \


LOCAL_LDFLAGS := $(LOCAL_PATH)/venc/mpp/osal/libosal.a

LOCAL_SHARED_LIBRARIES := \
    librga \
    liblog \
    libnativewindow \
    libandroid \
    libutils \
    libcutils \
    libmediandk \
    libEncodermpp

include $(BUILD_SHARED_LIBRARY)