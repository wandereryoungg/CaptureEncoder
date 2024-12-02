#define LOG_TAG "NativeCaptureModel"

#include "RgaCropScale.h"

#include <RockchipRga.h>
#include <hardware/hardware_rockchip.h>
#include <log/log.h>
#include <utils/Trace.h>

#include "im2d_api/im2d_common.h"

#define TARGET_RK3588

using namespace android;

void RgaCropScale::convertFormat(int srcWidth, int srcHeight, int srcFd,
                                 void* srcAddr, int srcFormat, int dstWidth,
                                 int dstHeight, int dstFd, void* dstAddr,
                                 int dstFormat) {
    ATRACE_CALL();
    ALOGI(
        "%s   srcWidth: %d srcHeight: %d srcAddr: %p dstWidth: %d dstHeight: "
        "%d dstAddr: %p",
        __func__, srcWidth, srcHeight, srcAddr, dstWidth, dstHeight, dstAddr);
    RockchipRga& rkRga(RockchipRga::get());
    rga_buffer_handle_t src_handle;
    rga_buffer_handle_t dst_handle;
    im_handle_param_t param;
    param.width = srcWidth;
    param.height = srcHeight;
    param.format = srcFormat;

    rga_info_t srcinfo;

    if (srcFd == -1) {
        srcinfo.fd = -1;
        srcinfo.virAddr = srcAddr;
#if defined(TARGET_RK3588)
        src_handle = importbuffer_virtualaddr(srcAddr, &param);
#endif
    } else {
        srcinfo.fd = srcFd;
#if defined(TARGET_RK3588)
        src_handle = importbuffer_fd(srcFd, &param);
#endif
    }

    srcinfo.mmuFlag = 1;
    srcinfo.rect.xoffset = 0;
    srcinfo.rect.yoffset = 0;
    srcinfo.rect.width = srcWidth;
    srcinfo.rect.height = srcHeight;
    srcinfo.rect.wstride = srcWidth;
    srcinfo.rect.hstride = srcHeight;
    srcinfo.rect.format = srcFormat;

    rga_info_t dstinfo;

    dstinfo.mmuFlag = 1;

    param.width = dstWidth;
    param.height = dstHeight;
    param.format = dstFormat;

    if (dstFd == -1) {
        dstinfo.fd = -1;
        dstinfo.virAddr = dstAddr;
#if defined(TARGET_RK3588)
        dst_handle = importbuffer_virtualaddr(dstAddr, &param);
#endif
    } else {
        dstinfo.fd = dstFd;
#if defined(TARGET_RK3588)
        dst_handle = importbuffer_fd(dstFd, &param);
#endif
    }

    dstinfo.rect.xoffset = 0;
    dstinfo.rect.yoffset = 0;
    dstinfo.rect.width = dstWidth;
    dstinfo.rect.height = dstHeight;
    dstinfo.rect.wstride = dstWidth;
    dstinfo.rect.hstride = dstHeight;
    dstinfo.rect.format = dstFormat;

    srcinfo.handle = src_handle;
    srcinfo.fd = 0;
    dstinfo.handle = dst_handle;
    dstinfo.fd = 0;

    rkRga.RkRgaBlit(&srcinfo, &dstinfo, NULL);

    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);
}