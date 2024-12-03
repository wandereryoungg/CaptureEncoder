//
// Created by Charlie on 2024/7/22.
//
#define LOG_TAG "NativeCaptureModel"

#include "CaptureModel.h"

#include <fcntl.h>
#include <log/log.h>
#include <unistd.h>
#include <utils/Trace.h>

#include <thread>

#include "RgaCropScale.h"

#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15

#define V4L2_TYPE_IS_META(type)             \
    ((type) == V4L2_BUF_TYPE_META_OUTPUT || \
     (type) == V4L2_BUF_TYPE_META_CAPTURE)

#define CLEAR(x) memset(&(x), 0, sizeof(x))

uint32_t sPreferFormat[] = {
    V4L2_PIX_FMT_NV12,
    V4L2_PIX_FMT_YUYV,
};

CaptureModel::CaptureModel(int cameraId, JavaVM* Jvm)
    : mCameraId(cameraId), globalJvm(Jvm) {
    ALOGI("%s   CaptureModel: %p", __func__, this);
    memset(gV4l2Buf, 0, sizeof(gV4l2Buf));
}

CaptureModel::~CaptureModel() {
    ALOGI("%s   CaptureModel: %p", __func__, this);
    stopCapture();
}

int CaptureModel::loadFd() { return mFd.load(); }

int CaptureModel::queryCapability() {
    int ret;
    if (mFd.load() < 0) {
        ALOGE("%s   queryCapability failed fd is wrong", __func__);
        return -errno;
    }
    ret = ioctl(mFd.load(), VIDIOC_QUERYCAP, &mCapability);
    if (ret) {
        ALOGE("%s   queryCapability QUERYCAP failed: %s", __func__,
              strerror(errno));
        return -errno;
    }

    if (mCapability.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        mBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else if (mCapability.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        mBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else if (mCapability.capabilities & V4L2_CAP_VIDEO_OUTPUT) {
        mBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else if (mCapability.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
        mBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else if (mCapability.capabilities & V4L2_CAP_META_CAPTURE) {
        mBufType = V4L2_BUF_TYPE_META_CAPTURE;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else if (mCapability.capabilities & V4L2_CAP_META_OUTPUT) {
        mBufType = V4L2_BUF_TYPE_META_OUTPUT;
        ALOGI("%s %d   mBufType: %d", __func__, __LINE__, mBufType);
    } else {
        ALOGE("%s   unsupported buffer type", __func__);
        return DEAD_OBJECT;
    }

    ALOGI("%s   success", __func__);
    return 0;
}

int CaptureModel::enumDeviceFmt() {
    if (mFd.load() < 0) {
        ALOGE("%s   enumDeviceFmt failed fd is wrong", __func__);
        return -errno;
    }
    memset(&mSelectParams, 0, sizeof(mSelectParams));

    v4l2_fmtdesc fmtDes;
    memset(&fmtDes, 0, sizeof(fmtDes));
    fmtDes.index = 0;
    fmtDes.type = mBufType;

    while (ioctl(mFd.load(), VIDIOC_ENUM_FMT, &fmtDes) != -1) {
        fmtDes.index++;
        ALOGI("%s   fd: %d support index: %d format: %d format description: %s",
              __func__, mFd.load(), fmtDes.index, fmtDes.pixelformat,
              (char*)(fmtDes.description));
        if (!mSelectParams.getFormat) {
            for (uint32_t format : sPreferFormat) {
                if (format == fmtDes.pixelformat) {
                    mSelectParams.getFormat = true;
                    mSelectParams.mV4l2Format = format;
                    break;
                }
            }
        }
    }

    ALOGI("%s   select v4l2 format: %c%c%c%c", __func__,
          mSelectParams.mV4l2Format & 0xFF,
          (mSelectParams.mV4l2Format >> 8) & 0xFF,
          (mSelectParams.mV4l2Format >> 16) & 0xFF,
          (mSelectParams.mV4l2Format >> 24) & 0xFF);

    mSelectParams.mMinDistance = 3840;
    v4l2_frmsizeenum frameSize{.index = 0,
                               .pixel_format = mSelectParams.mV4l2Format};
    for (; ioctl(mFd, VIDIOC_ENUM_FRAMESIZES, &frameSize) == 0;
         frameSize.index++) {
        if (frameSize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            ALOGI("%s   index:%d   format:%c%c%c%c   w:%d   h:%d", __func__,
                  frameSize.index, mSelectParams.mV4l2Format & 0xFF,
                  (mSelectParams.mV4l2Format >> 8) & 0xFF,
                  (mSelectParams.mV4l2Format >> 16) & 0xFF,
                  (mSelectParams.mV4l2Format >> 24) & 0xFF,
                  frameSize.discrete.width, frameSize.discrete.height);

            if (frameSize.discrete.height > frameSize.discrete.width) {
                continue;
            }
            int distance = std::abs((int)(frameSize.discrete.width - mWidth));
            if (distance < mSelectParams.mMinDistance) {
                mSelectParams.mV4l2Width = frameSize.discrete.width;
                mSelectParams.mV4l2Height = frameSize.discrete.height;
                mSelectParams.mMinDistance = distance;
            }
        }
    }

    ALOGI("%s   select v4l2 format: %c%c%c%c width: %d height: %d", __func__,
          mSelectParams.mV4l2Format & 0xFF,
          (mSelectParams.mV4l2Format >> 8) & 0xFF,
          (mSelectParams.mV4l2Format >> 16) & 0xFF,
          (mSelectParams.mV4l2Format >> 24) & 0xFF, mSelectParams.mV4l2Width,
          mSelectParams.mV4l2Height);

    return 0;
}

int CaptureModel::getDeviceFmt() {
    int result;
    if (mFd.load() < 0) {
        ALOGE("%s   getDeviceFmt failed fd is wrong", __func__);
        return -errno;
    }

    memset(&mFormat, 0, sizeof(mFormat));
    mFormat.type = mBufType;
    result = ioctl(mFd.load(), VIDIOC_G_FMT, &mFormat);
    if (result) {
        ALOGE("%s   fd: %d getDeviceFmt VIDIOC_G_FMT failed errno: %s",
              __func__, mFd.load(), strerror(errno));
    } else {
        ALOGI(
            "%s   fd: %d getDeviceFmt current format: %c%c%c%c width: %d "
            "height: %d",
            __func__, mFd.load(), mFormat.fmt.pix.pixelformat & 0xFF,
            (mFormat.fmt.pix.pixelformat >> 8) & 0xFF,
            (mFormat.fmt.pix.pixelformat >> 16) & 0xFF,
            (mFormat.fmt.pix.pixelformat >> 24) & 0xFF, mFormat.fmt.pix.width,
            mFormat.fmt.pix.height);
        mSelectParams.mV4l2Width = mFormat.fmt.pix.width;
        mSelectParams.mV4l2Height = mFormat.fmt.pix.height;
    }

    return result;
}

int CaptureModel::v4l2StreamOff() {
    if (!mV4l2Streaming) {
        return 0;
    }
    if (mFd.load() < 0) {
        ALOGE("%s   v4l2StreamOff failed fd is wrong", __func__);
        return -errno;
    }
    v4l2_buf_type capture_type;
    capture_type = mBufType;
    if (ioctl(mFd.load(), VIDIOC_STREAMOFF, &capture_type)) {
        ALOGE("%s   v4l2StreamOff VIDIOC_STREAMOFF failed: %s", __func__,
              strerror(errno));
        return -errno;
    }

    for (unsigned int i = 0; i < V4L2_BUFFER_COUNT; i++) {
        if (gV4l2Buf[i].start && gV4l2Buf[i].start != MAP_FAILED) {
            if (munmap(gV4l2Buf[i].start, gV4l2Buf[i].length) == -1) {
                ALOGE("%s   fd: %d v4l2StreamOff munmap failed errno: %s",
                      __func__, mFd.load(), strerror(errno));
            } else {
                ALOGI("%s   fd: %d v4l2StreamOff munmap start: %p length: %d",
                      __func__, mFd.load(), gV4l2Buf[i].start,
                      gV4l2Buf[i].length);
            }
            if (gV4l2Buf[i].exportFd) {
                close(gV4l2Buf[i].exportFd);
            }
        }
    }
    ALOGI("%s   success", __func__);
    mV4l2Streaming = false;
    return 0;
}

int CaptureModel::v4l2SetFmt() {
    if (mFd.load() < 0) {
        ALOGE("%s   v4l2SetFmt failed fd is wrong", __func__);
        return -errno;
    }
    v4l2_format fmt;
    fmt.type = mBufType;
    fmt.fmt.pix.width = mSelectParams.mV4l2Width;
    fmt.fmt.pix.height = mSelectParams.mV4l2Height;
    fmt.fmt.pix.pixelformat = mSelectParams.mV4l2Format;
    if (V4L2_TYPE_IS_META(mBufType)) {
        ALOGE("%s   setting field for meta format is not allowed.", __func__);
    } else if (V4L2_TYPE_IS_MULTIPLANAR(mBufType)) {
        fmt.fmt.pix_mp.field = 0;
    } else {
        fmt.fmt.pix.field = 0;
    }
    if (V4L2_TYPE_IS_META(mBufType)) {
        ALOGE("%s   setting bytesperline for meta format is not allowed.",
              __func__);
    } else if (V4L2_TYPE_IS_MULTIPLANAR(mBufType)) {
        fmt.fmt.pix_mp.plane_fmt[0].bytesperline = mSelectParams.mV4l2Width;
    } else {
        fmt.fmt.pix.bytesperline = mSelectParams.mV4l2Width;
    }
    int ret = ioctl(mFd.load(), VIDIOC_S_FMT, &fmt);
    if (ret) {
        ALOGE("%s   v4l2SetFmt VIDIOC_S_FMT failed: %s", __func__,
              strerror(errno));
        return -errno;
    }
    ALOGI("%s   success", __func__);
    return ret;
}

int CaptureModel::v4l2ReqBuf() {
    if (mFd.load() < 0) {
        ALOGE("%s   v4l2ReqBuf failed fd is wrong", __func__);
        return -errno;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUFFER_COUNT;
    req.memory = V4L2_MEMORY_MMAP;

    req.type = mBufType;

    if (ioctl(mFd.load(), VIDIOC_REQBUFS, &req)) {
        ALOGE("%s   fd: %d v4l2ReqBuf VIDIOC_REQBUFS failed, errno: %s",
              __func__, mFd.load(), strerror(errno));
        return -errno;
    }

    if (req.count < V4L2_BUFFER_COUNT) {
        ALOGE("%s   fd: %d v4l2ReqBuf low buffer memory on device, errno: %s",
              __func__, mFd.load(), strerror(errno));
        return -errno;
    }

    for (unsigned int i = 0; i < req.count; i++) {
        v4l2_buffer v4l2_buf;
        v4l2_buf.flags = 0x0;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;
        v4l2_buf.index = i;
        v4l2_buf.type = mBufType;
        if (V4L2_TYPE_IS_MULTIPLANAR(mBufType)) {
            struct v4l2_plane plane;
            CLEAR(plane);
            mPlanes.push_back(plane);
            v4l2_buf.m.planes = mPlanes.data();
            v4l2_buf.length = 1;
        }
        ALOGI("%s   VIDIOC_QUERYBUF mFd: %d memType: %d mBufType: %d index: %d",
              __func__, mFd.load(), v4l2_buf.memory, v4l2_buf.type,
              v4l2_buf.index);
        if (ioctl(mFd.load(), VIDIOC_QUERYBUF, &v4l2_buf)) {
            ALOGE("%s   fd: %d v4l2ReqBuf VIDIOC_QUERYBUF failed, errno: %s",
                  __func__, mFd.load(), strerror(errno));
            return -errno;
        }
        int length = 0;
        int offset = 0;

        if (V4L2_TYPE_IS_META(mBufType)) {
            length = mFormat.fmt.meta.buffersize;
        } else {
            bool mp = V4L2_TYPE_IS_MULTIPLANAR(mBufType);
            length = mp ? v4l2_buf.m.planes[0].length : v4l2_buf.length;
            offset = mp ? v4l2_buf.m.planes[0].m.mem_offset : v4l2_buf.m.offset;
        }
        gV4l2Buf[i].index = i;
        gV4l2Buf[i].length = length;
        gV4l2Buf[i].offset = offset;
        ALOGI("%s   mmap length: %d offset: %d", __func__, length, offset);
        // 映射内存
        gV4l2Buf[i].start = mmap(NULL, length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, mFd.load(), offset);
        if (MAP_FAILED == gV4l2Buf[i].start) {
            ALOGE("%s   fd: %d v4l2ReqBuf mmap failed, errno: %s", __func__,
                  mFd.load(), strerror(errno));
            return -errno;
        } else {
            ALOGI("%s   fd: %d v4l2ReqBuf mmap start: %p length: %d", __func__,
                  mFd.load(), gV4l2Buf[i].start, gV4l2Buf[i].length);
        }
        struct v4l2_exportbuffer ebuf;
        CLEAR(ebuf);
        ebuf.type = mBufType;
        ebuf.index = i;
        int ret = ioctl(mFd.load(), VIDIOC_EXPBUF, &ebuf);
        if (ret < 0) {
            ALOGE("%s   VIDIOC_EXPBUF failed ret: %s", __func__, strerror(errno));
            return -errno;
        }
        gV4l2Buf[i].exportFd = ebuf.fd;
        ALOGI("%s   VIDIOC_EXPBUF index: %d exportFd: %d", __func__, i, gV4l2Buf[i].exportFd);
    }

    ALOGI("%s   v4l2_req_buf ok", __func__);

    return 0;
}

int CaptureModel::v4l2SetFps() {
    if (mFd.load() < 0) {
        ALOGE("%s   v4l2SetFps failed fd is wrong", __func__);
        return -errno;
    }
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = mBufType;

    int ret = ioctl(mFd.load(), VIDIOC_G_PARM, &parm);
    if (ret != 0) {
        ALOGE("%s   device does not support VIDIOC_G_PARM", __func__);
        return -errno;
    }
    // Now check if the device is able to accept a capture frame rate set.
    if (!(parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        ALOGE("%s: device does not support V4L2_CAP_TIMEPERFRAME", __func__);
        return -EINVAL;
    }

    parm.parm.capture.timeperframe.denominator = mFps;
    parm.parm.capture.timeperframe.numerator = 1;
    int result = ioctl(mFd.load(), VIDIOC_S_PARM, &parm);
    if (result) {
        ALOGE("%s   fd:%d v4l2SetFps VIDIOC_S_PARM failed errno: %s", __func__,
              mFd.load(), strerror(errno));
        return -errno;
    }

    return result;
}

int CaptureModel::v4l2StreamOn() {
    if (mFd.load() < 0) {
        ALOGE("%s   v4l2StreamOn failed fd is wrong", __func__);
        return -errno;
    }

    unsigned int i = 0;
    for (i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.type = mBufType;
        if (V4L2_TYPE_IS_MULTIPLANAR(mBufType)) {
            buf.m.planes = mPlanes.data();
            buf.length = 1;
        }
        // 将缓冲帧放入队列
        if (ioctl(mFd.load(), VIDIOC_QBUF, &buf)) {
            ALOGE("%s   fd:%d VIDIOC_QBUF failed errno: %s", __func__,
                  mFd.load(), strerror(errno));
            return -errno;
        }
    }

    // 开始捕捉图像数据
    enum v4l2_buf_type type;
    type = mBufType;

    if (ioctl(mFd.load(), VIDIOC_STREAMON, &type)) {
        ALOGE("%s   fd:%d VIDIOC_STREAMON failed errno: %s", __func__,
              mFd.load(), strerror(errno));
        return -errno;
    }
    mV4l2Streaming = true;
    return 0;
}

int CaptureModel::addEncoderUnit(jobject& javaEncoder) {
    std::lock_guard<std::mutex> lk(mEncoderLock);
    mJavaEncoders[mEncoderId] = javaEncoder;
    mEncoderId++;
    return mEncoderId - 1;
}

jobject CaptureModel::removeEncoderUnit(int encoderId) {
    std::lock_guard<std::mutex> lk(mEncoderLock);
    auto iter = mJavaEncoders.find(encoderId);
    if (iter != mJavaEncoders.end()) {
        jobject javaObj = iter->second;
        mJavaEncoders.erase(encoderId);
        return javaObj;
    } else {
        ALOGI("%s   encoderId: %d not exist", __func__, encoderId);
        return nullptr;
    }
}

int CaptureModel::querySensorTimings() {
    int ret = NO_ERROR;

    int fd = open("/dev/v4l-subdev5", O_RDWR);
    if (fd < 0) {
        ALOGE("%s   open device failed: %s", __func__, strerror(errno));
        return -errno;
    }
    struct v4l2_dv_timings timings;
    ret = ioctl(fd, VIDIOC_SUBDEV_QUERY_DV_TIMINGS, &timings);
    ALOGI("%s   ret:%d   I:%d   wxh:%dx%d", __func__, ret,
          timings.bt.interlaced, timings.bt.width, timings.bt.height);
    if (ret < 0) {
        ALOGE("%s   VIDIOC_SUBDEV_QUERY_DV_TIMINGS failed: %s", __func__,
              strerror(errno));
        close(fd);
        return UNKNOWN_ERROR;
    }
    close(fd);
    mSelectParams.mV4l2Width = timings.bt.width;
    mSelectParams.mV4l2Height = timings.bt.height;
    ALOGI("%s done", __func__);
    return ret;
}

int CaptureModel::getSensorFormat() {
    int ret = NO_ERROR;

    int fd = open("/dev/v4l-subdev5", O_RDWR);
    if (fd < 0) {
        ALOGE("%s   open device failed: %s", __func__, strerror(errno));
        return -errno;
    }
    CLEAR(mSubFormat);
    ret = ioctl(fd, VIDIOC_SUBDEV_G_FMT, &mSubFormat);
    if (ret < 0) {
        ALOGE("%s   VIDIOC_SUBDEV_G_FMT failed: %s", __func__, strerror(errno));
        close(fd);
        return UNKNOWN_ERROR;
    }

    ALOGI(
        "%s   VIDIOC_SUBDEV_G_FMT: pad: %d, which: %d, width: %d, "
        "height: %d, format: 0x%x, field: %d, color space: %d",
        __func__, mSubFormat.pad, mSubFormat.which, mSubFormat.format.width,
        mSubFormat.format.height, mSubFormat.format.code,
        mSubFormat.format.field, mSubFormat.format.colorspace);
    close(fd);
    ALOGI("%s done", __func__);
    return ret;
}

int CaptureModel::setSensorFormat() {
    int ret = NO_ERROR;

    int fd = open("/dev/v4l-subdev5", O_RDWR);
    if (fd < 0) {
        ALOGE("%s   open device failed: %s", __func__, strerror(errno));
        return -errno;
    }

    CLEAR(mSubFormat);
    mSubFormat.pad = 0;
    mSubFormat.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    mSubFormat.format.code = 0x2006;
    mSubFormat.format.width = 3840;
    mSubFormat.format.height = 2160;
    mSubFormat.format.field = 0;
    mSubFormat.format.quantization = 0;

    ALOGI(
        "%s   VIDIOC_SUBDEV_S_FMT: pad: %d, which: %d, width: %d, "
        "height: %d, format: 0x%x, field: %d, color space: %d",
        __func__, mSubFormat.pad, mSubFormat.which, mSubFormat.format.width,
        mSubFormat.format.height, mSubFormat.format.code,
        mSubFormat.format.field, mSubFormat.format.colorspace);

    ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &mSubFormat);
    if (ret < 0) {
        ALOGE("%s   VIDIOC_SUBDEV_S_FMT failed: %s", __func__, strerror(errno));
        close(fd);
        return UNKNOWN_ERROR;
    }

    close(fd);
    ALOGI("%s done", __func__);
    return NO_ERROR;
}

int CaptureModel::setSensorInterval() {
    int ret = NO_ERROR;

    int fd = open("/dev/v4l-subdev5", O_RDWR);
    if (fd < 0) {
        ALOGE("%s   open device failed: %s", __func__, strerror(errno));
        return -errno;
    }

    struct v4l2_subdev_frame_interval finterval {
        .pad = 0, .interval.numerator = 10000,
        .interval.denominator = (__u32)10000 * mFps,
    };

    ALOGI(
        "%s   VIDIOC_SUBDEV_S_FRAME_INTERVAL: pad: %d, numerator %d, "
        "denominator %d",
        __func__, finterval.pad, finterval.interval.numerator,
        finterval.interval.denominator);
    ret = ioctl(fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &finterval);
    if (ret < 0) {
        ALOGE("%s   VIDIOC_SUBDEV_S_FRAME_INTERVAL failed: %s", __func__,
              strerror(errno));
        close(fd);
        return NO_ERROR;
    }
    close(fd);
    ALOGI("%s done", __func__);
    return NO_ERROR;
}

int CaptureModel::startCapture(ANativeWindow* nativeWindow, int width,
                               int height, int fps) {
    ALOGI("%s   width: %d height: %d fps: %d", __func__, width, height, fps);
    mFd = open("/dev/video22", O_RDWR);
    if (mFd.load() < 0) {
        ALOGE("%s   open device failed: %s", __func__, strerror(errno));
        return -errno;
    }

    std::unique_lock<std::mutex> lock(mCaptureLock);
    if (mV4l2Streaming) {
        ALOGE("%s   camera is already on", __func__);
        return 0;
    }

    mWidth = width;
    mHeight = height;
    mFps = fps;

    int ret = querySensorTimings();
    if (ret < 0) {
        ALOGE("%s   querySensorTimings failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = getSensorFormat();
    if (ret < 0) {
        ALOGE("%s   getSensorFormat failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = setSensorFormat();
    if (ret < 0) {
        ALOGE("%s   setSensorFormat failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = setSensorInterval();
    if (ret < 0) {
        ALOGE("%s   setSensorInterval failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = queryCapability();
    if (ret < 0) {
        ALOGE("%s   queryCapability failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = enumDeviceFmt();
    if (ret < 0) {
        ALOGE("%s   getDeviceFmt failed: %s", __func__, strerror(errno));
        return ret;
    }
    /*
    ret = v4l2StreamOff();
    if (ret < 0) {
        ALOGE("%s   v4l2StreamOff failed: %s", __func__, strerror(errno));
        return ret;
    }
    */
    if (mSelectParams.mV4l2Width == 0 || mSelectParams.mV4l2Height == 0) {
        mSelectParams.mV4l2Width = 3840;
        mSelectParams.mV4l2Height = 2160;
    }
    ret = v4l2SetFmt();
    if (ret < 0) {
        ALOGE("%s   v4l2SetFmt failed: %s", __func__, strerror(errno));
        return ret;
    }
    /*
    ret = v4l2SetFps();
    if (ret < 0) {
        ALOGE("%s   v4l2SetFps failed: %s", __func__, strerror(errno));
        return ret;
    }
    */

    ret = getDeviceFmt();
    if (ret < 0) {
        ALOGE("%s   getDeviceFmt failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = v4l2ReqBuf();
    if (ret < 0) {
        ALOGE("%s   v4l2ReqBuf failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = v4l2StreamOn();
    if (ret < 0) {
        ALOGE("%s   v4l2StreamOn failed: %s", __func__, strerror(errno));
        return ret;
    }

    if (nativeWindow) {
        sp<IProcessUnit> previewUnit =
            new PreviewUnit(this, nativeWindow, mWidth, mHeight);
        previewUnit->run("PreviewUnit");
        mProcessList.push_back(previewUnit);
    } else {
        ALOGE("%s   nativeWindow is nullptr", __func__);
    }

    {
        std::lock_guard<std::mutex> lk(mEncoderLock);
        for (const auto& pair : mJavaEncoders) {
            JNIEnv* jniEnv = getJniEnv(globalJvm);
            if (jniEnv) {
                jclass clazz = jniEnv->GetObjectClass(pair.second);

                int encoderWidth = 0, encoderHeight = 0;

                jfieldID widthField = jniEnv->GetFieldID(clazz, "mWidth", "I");
                if (widthField == nullptr) {
                    ALOGE("%s   cannot get widthField errno: %s", __func__, strerror(errno));
                } else {
                    encoderWidth = jniEnv->GetIntField(pair.second, widthField);
                }

                jfieldID heightField = jniEnv->GetFieldID(clazz, "mHeight", "I");
                if (heightField == nullptr) {
                    ALOGE("%s   cannot get heightField errno: %s", __func__, strerror(errno));
                } else {
                    encoderHeight = jniEnv->GetIntField(pair.second, heightField);
                }

                ALOGI("%s   encoderWidth: %d encoderHeight: %d", __func__, encoderWidth, encoderHeight);

                sp<IProcessUnit> encodeUnit = nullptr;
                if (mSelectParams.mV4l2Width != encoderWidth) {
                    encodeUnit = new EncoderUnit(this, globalJvm, pair.second, mFps);
                } else {
                    encodeUnit = new MppEncoderUnit(this, gV4l2Buf, globalJvm, pair.second, mFps);
                }
                encodeUnit->run("EncoderUnit");
                mProcessList.push_back(encodeUnit);

            } else {
                ALOGE("%s   cannot get jni env errno: %s", __func__, strerror(errno));
            }
        }
    }

    mPollThread =
        new PollThread(this, mProcessList, mSelectParams.mV4l2Width,
                       mSelectParams.mV4l2Height, mSelectParams.mV4l2Format);
    mPollThread->run("PollThread");

    return 0;
}

int CaptureModel::stopCapture() {
    std::unique_lock<std::mutex> lock(mCaptureLock);

    if (!mV4l2Streaming) {
        ALOGE("%s   camera is already off", __func__);
        return 0;
    }
    if (mPollThread) {
        mPollThread->requestExit();
        mPollThread->join();
        mPollThread.clear();
        mPollThread = nullptr;
    }

    for (const auto& unit : mProcessList) {
        const sp<IProcessUnit>& processUnit = unit;
        processUnit->requestExit();
        processUnit->join();
    }
    mProcessList.clear();

    ALOGI("%s   mFd: %d", __func__, mFd.load());
    if (mFd.load() > 0) {
        ALOGI("%s   mFd: %d start stream off", __func__, mFd.load());
        v4l2StreamOff();
        close(mFd.load());
        mFd.store(-1);
        ALOGI("%s   mFd: %d end stream off", __func__, mFd.load());
    }
    return 0;
}

CaptureModel::PollThread::PollThread(sp<CaptureModel> captureModel,
                                     std::list<sp<IProcessUnit>> processList,
                                     uint32_t width, uint32_t height,
                                     uint32_t format)
    : mCaptureModel(captureModel),
      mProcessList(processList),
      mWidth(width),
      mHeight(height),
      mFormat(format) {
    ALOGI("%s   PollThread: %p", __func__, this);
}

CaptureModel::PollThread::~PollThread() {
    ALOGI("%s   PollThread: %p", __func__, this);
}

status_t CaptureModel::PollThread::readyToRun() {
    ALOGI("%s   PollThread: %p", __func__, this);
    return NO_ERROR;
}

bool CaptureModel::PollThread::threadLoop() {
    ATRACE_CALL();
    ALOGI("%s", __func__);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(mCaptureModel->loadFd(), &fds);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    v4l2_buffer buffer{};
    buffer.type = mCaptureModel->mBufType;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (V4L2_TYPE_IS_MULTIPLANAR(mCaptureModel->mBufType)) {
        buffer.m.planes = mCaptureModel->mPlanes.data();
        buffer.length = 1;
    }

    int r = select(mCaptureModel->loadFd() + 1, &fds, NULL, NULL, &tv);
    if (-1 == r) {
        ALOGE("%s   select error: %s", __func__, strerror(errno));
    } else if (0 == r) {
        ALOGE("%s   select timeout: %s", __func__, strerror(errno));
    } else {
        if (ioctl(mCaptureModel->loadFd(), VIDIOC_DQBUF, &buffer) < 0) {
            ALOGE("%s   VIDIOC_DQBUF fails: %s", __func__, strerror(errno));
            return true;
        }

        if (V4L2_TYPE_IS_MULTIPLANAR(mCaptureModel->mBufType)) {
            int bytesUsed = buffer.m.planes[0].length;
            void* start = mCaptureModel->gV4l2Buf[buffer.index].start;
            ALOGI("%s   index: %d start: %p bytesUsed: %d line: %d", __func__,
                  buffer.index, start, bytesUsed, __LINE__);
            postProcess(start, buffer.index);
        } else {
            int bytesUsed = buffer.bytesused;
            void* start = mCaptureModel->gV4l2Buf[buffer.index].start;
            ALOGI("%s   index: %d start: %p bytesUsed: %d line: %d", __func__,
                  buffer.index, start, bytesUsed, __LINE__);
            postProcess(start, buffer.index);
        }
    }

    return true;
}

void CaptureModel::PollThread::postProcess(void* start, int index) {
    std::lock_guard<std::mutex> lk(mProcessBufLock);
    std::shared_ptr<IProcessUnit::ProcessBuf> processBuf =
        std::make_shared<IProcessUnit::ProcessBuf>();
    processBuf->index = index;
    processBuf->start = start;
    processBuf->width = mWidth;
    processBuf->height = mHeight;
    processBuf->format = mFormat;
    processBuf->processNum = mProcessList.size();
    ALOGI("%s   processBuf index: %d processNum: %d", __func__, processBuf->index, processBuf->processNum);
    for (auto iter : mProcessList) {
        iter->processBuffer(processBuf);
    }
}

void CaptureModel::notifyProcessDone(std::shared_ptr<IProcessUnit::ProcessBuf>& processBuf) {
    if (mPollThread) {
        mPollThread->returnCameraBuffer(processBuf);
    }

}

void CaptureModel::PollThread::returnCameraBuffer(std::shared_ptr<IProcessUnit::ProcessBuf>& processBuf) {
    std::lock_guard<std::mutex> lk(mProcessBufLock);
    processBuf->processNum--;
    ALOGI("%s   processBuf index: %d processNum: %d", __func__, processBuf->index, processBuf->processNum);
    if (processBuf->processNum == 0) {
        v4l2_buffer buffer{};
        buffer.index = processBuf->index;
        buffer.type = mCaptureModel->mBufType;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (V4L2_TYPE_IS_MULTIPLANAR(mCaptureModel->mBufType)) {
            buffer.m.planes = mCaptureModel->mPlanes.data();
            buffer.length = 1;
        }
        if (ioctl(mCaptureModel->loadFd(), VIDIOC_QBUF, &buffer) < 0) {
            ALOGE("%s   VIDIOC_QBUF index: %d fails: %s", __func__,
                  buffer.index, strerror(errno));
        }

        ALOGI("%s   VIDIOC_QBUF index: %d", __func__, buffer.index);
        showDebugFPS();
    }
}

void CaptureModel::PollThread::showDebugFPS() {
	double fps = 0;
	mFrameCount++;
	nsecs_t now = systemTime();
	nsecs_t diff = now - mLastFpsTime;
	if ((unsigned long)diff > 2000000000) {
		fps = (((double)(mFrameCount - mLastFrameCount)) * (double)(1000000000)) / (double)diff;
		ALOGI("%s   Preview FPS: %.4f   mFrameCount: %d", __func__, fps, mFrameCount);
		mLastFpsTime = now;
		mLastFrameCount = mFrameCount;
	}
}
