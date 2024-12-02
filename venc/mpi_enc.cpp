//
// Created by Charlie on 2024/7/31.
//
#define LOG_TAG "NativeMppEncoder"

#include "mpi_enc.h"

#include <log/log.h>

MppEncoder::MppEncoder() { ALOGI("%s   MppEncoder: %p", __func__, this); }

MppEncoder::~MppEncoder() { ALOGI("%s   MppEncoder: %p", __func__, this); }

MPP_RET MppEncoder::venc_init(RK_S32 chn, VENC_ATTR_t *venc_attr, v4l2Buffer* buffer, int buf_len) {
    ALOGI("%s   MppEncoder: %p chn: %d venc_attr: %p", __func__, this, chn,
          venc_attr);
    MPP_RET ret = MPP_OK;
    MppPollType timeout = MppPollType::MPP_POLL_MAX;

    VENC_MPI_ATTR *p = &venc_mpi_attr;
    memset(p, 0, sizeof(VENC_MPI_ATTR));

    p->chn = chn;
    ret = venc_mpi_init(p, venc_attr);
    if (ret) {
        ALOGE("%s venc mpi init failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = mpp_buffer_group_get_internal(
        &p->buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
    if (ret) {
        ALOGE("%s failed to get mpp buffer group ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret =
        mpp_buffer_get(p->buf_grp, &p->frm_buf, p->frame_size + p->header_size);
    if (ret) {
        ALOGE("%s failed to get buffer for input frame ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = mpp_buffer_get(p->buf_grp, &p->pkt_buf, p->frame_size);
    if (ret) {
        ALOGE("%s failed to get buffer for output packet ret: %d", __func__,
              ret);
        goto VENC_ERROR;
    }

    ret = mpp_buffer_get(p->buf_grp, &p->md_info, p->mdinfo_size);
    if (ret) {
        ALOGE("%s failed to get buffer for motion info output packet ret: %d",
              __func__, ret);
        goto VENC_ERROR;
    }

    ret = mpp_create(&p->ctx, &p->mpi);
    if (ret) {
        ALOGE("%s mpp_create failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }
    ALOGI("%s   encoder: %p start w: %d h: %d type: %d", __func__, p->ctx,
          p->width, p->height, p->type);

    ret = p->mpi->control(p->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (MPP_OK != ret) {
        ALOGE("%s mpi control set output timeout ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
    if (ret) {
        ALOGE("%s mpp_init failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = mpp_enc_cfg_init(&p->cfg);
    if (ret) {
        ALOGE("%s mpp_enc_cfg_init failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = p->mpi->control(p->ctx, MPP_ENC_GET_CFG, p->cfg);
    if (ret) {
        ALOGE("%s get enc cfg failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    ret = venc_mpp_cfg_setup(p);
    if (ret) {
        ALOGE("%s test mpp setup failed ret: %d", __func__, ret);
        goto VENC_ERROR;
    }

    for (int i = 0; i < V4L2_BUFFER_COUNT; i++) {
        MppBufferInfo info;
        memset(&info, 0, sizeof(MppBufferInfo));
        info.type = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd =  (buffer + i)->exportFd;
        info.size = buf_len & 0x07ffffff;
        info.index = (buf_len & 0xf8000000) >> 27;
        mpp_buffer_import(&mppBuffer[i], &info);
        ALOGI("%s   exportFd: %d mpp_buffer_import: %p", __func__, info.fd, &mppBuffer[i]);
    }

    return ret;

VENC_ERROR:

    if (p->ctx) {
        mpp_destroy(p->ctx);
        p->ctx = NULL;
    }

    if (p->cfg) {
        mpp_enc_cfg_deinit(p->cfg);
        p->cfg = NULL;
    }

    if (p->frm_buf) {
        mpp_buffer_put(p->frm_buf);
        p->frm_buf = NULL;
    }

    if (p->pkt_buf) {
        mpp_buffer_put(p->pkt_buf);
        p->pkt_buf = NULL;
    }

    if (p->md_info) {
        mpp_buffer_put(p->md_info);
        p->md_info = NULL;
    }

    if (p->buf_grp) {
        mpp_buffer_group_put(p->buf_grp);
        p->buf_grp = NULL;
    }

    return ret;
}

MPP_RET MppEncoder::venc_mpi_init(VENC_MPI_ATTR *venc_mpi_attr,
                                  VENC_ATTR *venc_attr) {
    venc_mpi_attr->width = venc_attr->width;
    venc_mpi_attr->height = venc_attr->height;

    venc_mpi_attr->hor_stride =
        mpi_enc_width_default_stride(venc_attr->width, venc_attr->format);
    venc_mpi_attr->ver_stride = venc_attr->height;
    venc_mpi_attr->fmt = venc_attr->format;
    venc_mpi_attr->type = venc_attr->type;
    venc_mpi_attr->bps = venc_attr->bps_target;
    venc_mpi_attr->bps_min = venc_attr->bps_min;
    venc_mpi_attr->bps_max = venc_attr->bps_max;
    venc_mpi_attr->rc_mode = venc_attr->rc_mode;
    venc_mpi_attr->gop_mode = venc_attr->gop_mode;
    venc_mpi_attr->gop_len = venc_attr->gop_len;
    venc_mpi_attr->vi_len = venc_attr->vi_len;

    venc_mpi_attr->fps_in_flex = venc_attr->fps_in_flex;
    venc_mpi_attr->fps_in_den = venc_attr->fps_in_den;
    venc_mpi_attr->fps_in_num = venc_attr->fps_in_num;
    venc_mpi_attr->fps_out_flex = venc_attr->fps_out_flex;
    venc_mpi_attr->fps_out_den = venc_attr->fps_out_den;
    venc_mpi_attr->fps_out_num = venc_attr->fps_out_num;

    venc_mpi_attr->qp_init = venc_attr->qp_init;
    venc_mpi_attr->qp_min = venc_attr->qp_min;
    venc_mpi_attr->qp_max = venc_attr->qp_max;
    venc_mpi_attr->qp_min_i = venc_attr->qp_min_i;
    venc_mpi_attr->qp_max_i = venc_attr->qp_max_i;

    venc_mpi_attr->mdinfo_size =
        (MPP_VIDEO_CodingHEVC == venc_mpi_attr->type)
            ? (MPP_ALIGN(venc_mpi_attr->hor_stride, 32) >> 5) *
                  (MPP_ALIGN(venc_mpi_attr->ver_stride, 32) >> 5) * 16
            : (MPP_ALIGN(venc_mpi_attr->hor_stride, 64) >> 6) *
                  (MPP_ALIGN(venc_mpi_attr->ver_stride, 16) >> 4) * 16;

    switch (venc_mpi_attr->fmt & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV420SP:
        case MPP_FMT_YUV420P: {
            venc_mpi_attr->frame_size =
                MPP_ALIGN(venc_mpi_attr->hor_stride, 64) *
                MPP_ALIGN(venc_mpi_attr->ver_stride, 64) * 3 / 2;
        } break;

        case MPP_FMT_YUV422_YUYV:
        case MPP_FMT_YUV422_YVYU:
        case MPP_FMT_YUV422_UYVY:
        case MPP_FMT_YUV422_VYUY:
        case MPP_FMT_YUV422P:
        case MPP_FMT_YUV422SP: {
            venc_mpi_attr->frame_size =
                MPP_ALIGN(venc_mpi_attr->hor_stride, 64) *
                MPP_ALIGN(venc_mpi_attr->ver_stride, 64) * 2;
        } break;
        case MPP_FMT_YUV400:
        case MPP_FMT_RGB444:
        case MPP_FMT_BGR444:
        case MPP_FMT_RGB555:
        case MPP_FMT_BGR555:
        case MPP_FMT_RGB565:
        case MPP_FMT_BGR565:
        case MPP_FMT_RGB888:
        case MPP_FMT_BGR888:
        case MPP_FMT_RGB101010:
        case MPP_FMT_BGR101010:
        case MPP_FMT_ARGB8888:
        case MPP_FMT_ABGR8888:
        case MPP_FMT_BGRA8888:
        case MPP_FMT_RGBA8888: {
            venc_mpi_attr->frame_size =
                MPP_ALIGN(venc_mpi_attr->hor_stride, 64) *
                MPP_ALIGN(venc_mpi_attr->ver_stride, 64);
        } break;

        default: {
            venc_mpi_attr->frame_size =
                MPP_ALIGN(venc_mpi_attr->hor_stride, 64) *
                MPP_ALIGN(venc_mpi_attr->ver_stride, 64) * 4;
        } break;
    }

    ALOGI(
        "venc_mpi_attr frame_size %zu, width %d, height %d, hor_stride %d, "
        "ver_stride %d",
        venc_mpi_attr->frame_size, venc_mpi_attr->width, venc_mpi_attr->height,
        venc_mpi_attr->hor_stride, venc_mpi_attr->ver_stride);

    if (MPP_FRAME_FMT_IS_FBC(venc_mpi_attr->fmt)) {
        if ((venc_mpi_attr->fmt & MPP_FRAME_FBC_MASK) == MPP_FRAME_FBC_AFBC_V1)
            venc_mpi_attr->header_size =
                MPP_ALIGN(MPP_ALIGN(venc_mpi_attr->width, 16) *
                              MPP_ALIGN(venc_mpi_attr->height, 16) / 16,
                          SZ_4K);
        else
            venc_mpi_attr->header_size = MPP_ALIGN(venc_mpi_attr->width, 16) *
                                         MPP_ALIGN(venc_mpi_attr->height, 16) /
                                         16;
    } else {
        venc_mpi_attr->header_size = 0;
    }

    return MPP_OK;
}

MPP_RET MppEncoder::venc_mpp_cfg_setup(VENC_MPI_ATTR *venc_mpi_attr) {
    MPP_RET ret;
    MppApi *mpi = venc_mpi_attr->mpi;
    MppCtx ctx = venc_mpi_attr->ctx;
    MppEncCfg cfg = venc_mpi_attr->cfg;

    RK_U32 rotation;
    RK_U32 mirroring;
    RK_U32 flip;
    RK_U32 gop_mode = venc_mpi_attr->gop_mode;
    MppEncRefCfg ref = NULL;

    /* setup default parameter */
    if (venc_mpi_attr->fps_in_den == 0) venc_mpi_attr->fps_in_den = 1;
    if (venc_mpi_attr->fps_in_num == 0) venc_mpi_attr->fps_in_num = 30;
    if (venc_mpi_attr->fps_out_den == 0) venc_mpi_attr->fps_out_den = 1;
    if (venc_mpi_attr->fps_out_num == 0) venc_mpi_attr->fps_out_num = 30;

    if (!venc_mpi_attr->bps)
        venc_mpi_attr->bps =
            venc_mpi_attr->width * venc_mpi_attr->height / 8 *
            (venc_mpi_attr->fps_out_num / venc_mpi_attr->fps_out_den);

    venc_mpi_attr->scene_mode = 0;
    mpp_enc_cfg_set_s32(cfg, "tune:scene_mode", venc_mpi_attr->scene_mode);
    mpp_enc_cfg_set_s32(cfg, "prep:width", venc_mpi_attr->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", venc_mpi_attr->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", venc_mpi_attr->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", venc_mpi_attr->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", venc_mpi_attr->fmt);

    ALOGD("venc_mpi_attr->width %d", venc_mpi_attr->width);
    ALOGD("venc_mpi_attr->height %d", venc_mpi_attr->height);
    ALOGD("venc_mpi_attr->hor_stride %d", venc_mpi_attr->hor_stride);
    ALOGD("venc_mpi_attr->ver_stride %d", venc_mpi_attr->ver_stride);
    ALOGD("venc_mpi_attr->fmt %d", venc_mpi_attr->fmt);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", venc_mpi_attr->rc_mode);

    ALOGD("venc_mpi_attr->rc_mode %d", venc_mpi_attr->rc_mode);

    /* fix input / output frame rate */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", venc_mpi_attr->fps_in_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", venc_mpi_attr->fps_in_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", venc_mpi_attr->fps_in_den);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", venc_mpi_attr->fps_out_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", venc_mpi_attr->fps_out_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", venc_mpi_attr->fps_out_den);

    ALOGD("venc_mpi_attr->fps_in_flex %d", venc_mpi_attr->fps_in_flex);
    ALOGD("venc_mpi_attr->fps_in_num %d", venc_mpi_attr->fps_in_num);
    ALOGD("venc_mpi_attr->fps_in_den %d", venc_mpi_attr->fps_in_den);
    ALOGD("venc_mpi_attr->fps_out_flex %d", venc_mpi_attr->fps_out_flex);
    ALOGD("venc_mpi_attr->fps_out_num %d", venc_mpi_attr->fps_out_num);
    ALOGD("venc_mpi_attr->fps_out_den %d", venc_mpi_attr->fps_out_den);

    /* drop frame or not when bitrate overflow */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_thd", 20); /* 20% of max bps */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_gap",
                        1); /* Do not continuous drop frame */

    /* setup bitrate for different rc_mode */
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", venc_mpi_attr->bps);

    ALOGD("venc_mpi_attr->bps %d", venc_mpi_attr->bps);

    switch (venc_mpi_attr->rc_mode) {
        case MPP_ENC_RC_MODE_FIXQP: {
            /* do not setup bitrate on FIXQP mode */
        } break;
        case MPP_ENC_RC_MODE_CBR: {
            /* CBR mode has narrow bound */
            mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
                                venc_mpi_attr->bps_max
                                    ? venc_mpi_attr->bps_max
                                    : venc_mpi_attr->bps * 17 / 16);
            mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
                                venc_mpi_attr->bps_min
                                    ? venc_mpi_attr->bps_min
                                    : venc_mpi_attr->bps * 15 / 16);
        } break;
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR: {
            /* VBR mode has wide bound */
            mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
                                venc_mpi_attr->bps_max
                                    ? venc_mpi_attr->bps_max
                                    : venc_mpi_attr->bps * 17 / 16);
            mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
                                venc_mpi_attr->bps_min
                                    ? venc_mpi_attr->bps_min
                                    : venc_mpi_attr->bps * 1 / 16);
        } break;
        default: {
            /* default use CBR mode */
            mpp_enc_cfg_set_s32(cfg, "rc:bps_max",
                                venc_mpi_attr->bps_max
                                    ? venc_mpi_attr->bps_max
                                    : venc_mpi_attr->bps * 17 / 16);
            mpp_enc_cfg_set_s32(cfg, "rc:bps_min",
                                venc_mpi_attr->bps_min
                                    ? venc_mpi_attr->bps_min
                                    : venc_mpi_attr->bps * 15 / 16);
        } break;
    }

    ALOGD("venc_mpi_attr->bps_max %d", venc_mpi_attr->bps_max);
    ALOGD("venc_mpi_attr->bps_min %d", venc_mpi_attr->bps_min);

    /* setup qp for different codec and rc_mode */
    switch (venc_mpi_attr->type) {
        case MPP_VIDEO_CodingAVC:
        case MPP_VIDEO_CodingHEVC: {
            switch (venc_mpi_attr->rc_mode) {
                case MPP_ENC_RC_MODE_FIXQP: {
                    RK_S32 fix_qp = venc_mpi_attr->qp_init;

                    mpp_enc_cfg_set_s32(cfg, "rc:qp_init", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_max", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_min", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 0);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_i", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_i", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_p", fix_qp);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_p", fix_qp);
                    ALOGD("fix_qp %d", fix_qp);
                } break;
                case MPP_ENC_RC_MODE_CBR:
                case MPP_ENC_RC_MODE_VBR:
                case MPP_ENC_RC_MODE_AVBR: {
                    mpp_enc_cfg_set_s32(
                        cfg, "rc:qp_init",
                        venc_mpi_attr->qp_init ? venc_mpi_attr->qp_init : -1);
                    mpp_enc_cfg_set_s32(
                        cfg, "rc:qp_max",
                        venc_mpi_attr->qp_max ? venc_mpi_attr->qp_max : 51);
                    mpp_enc_cfg_set_s32(
                        cfg, "rc:qp_min",
                        venc_mpi_attr->qp_min ? venc_mpi_attr->qp_min : 10);
                    mpp_enc_cfg_set_s32(
                        cfg, "rc:qp_max_i",
                        venc_mpi_attr->qp_max_i ? venc_mpi_attr->qp_max_i : 51);
                    mpp_enc_cfg_set_s32(
                        cfg, "rc:qp_min_i",
                        venc_mpi_attr->qp_min_i ? venc_mpi_attr->qp_min_i : 10);
                    mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 2);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_i",
                                        venc_mpi_attr->fqp_min_i
                                            ? venc_mpi_attr->fqp_min_i
                                            : 10);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_i",
                                        venc_mpi_attr->fqp_max_i
                                            ? venc_mpi_attr->fqp_max_i
                                            : 51);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_p",
                                        venc_mpi_attr->fqp_min_p
                                            ? venc_mpi_attr->fqp_min_p
                                            : 10);
                    mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_p",
                                        venc_mpi_attr->fqp_max_p
                                            ? venc_mpi_attr->fqp_max_p
                                            : 51);

                    ALOGD("qp_init %d",
                          venc_mpi_attr->qp_init ? venc_mpi_attr->qp_init : -1);
                    ALOGD("qp_max %d",
                          venc_mpi_attr->qp_max ? venc_mpi_attr->qp_max : 51);
                    ALOGD("qp_min %d",
                          venc_mpi_attr->qp_min ? venc_mpi_attr->qp_min : 10);
                    ALOGD("qp_max_i %d", venc_mpi_attr->qp_max_i
                                             ? venc_mpi_attr->qp_max_i
                                             : 51);
                    ALOGD("qp_min_i %d", venc_mpi_attr->qp_min_i
                                             ? venc_mpi_attr->qp_min_i
                                             : 10);
                    ALOGD("fqp_min_i %d", venc_mpi_attr->fqp_min_i
                                              ? venc_mpi_attr->fqp_min_i
                                              : 10);
                    ALOGD("fqp_max_i %d", venc_mpi_attr->fqp_max_i
                                              ? venc_mpi_attr->fqp_max_i
                                              : 51);
                    ALOGD("fqp_min_p %d", venc_mpi_attr->fqp_min_p
                                              ? venc_mpi_attr->fqp_min_p
                                              : 10);
                    ALOGD("fqp_max_p %d", venc_mpi_attr->fqp_max_p
                                              ? venc_mpi_attr->fqp_max_p
                                              : 51);

                } break;
                default: {
                    ALOGE("unsupport encoder rc mode %d",
                          venc_mpi_attr->rc_mode);
                } break;
            }
        } break;
        case MPP_VIDEO_CodingVP8: {
            /* vp8 only setup base qp range */
            mpp_enc_cfg_set_s32(
                cfg, "rc:qp_init",
                venc_mpi_attr->qp_init ? venc_mpi_attr->qp_init : 40);
            mpp_enc_cfg_set_s32(
                cfg, "rc:qp_max",
                venc_mpi_attr->qp_max ? venc_mpi_attr->qp_max : 127);
            mpp_enc_cfg_set_s32(
                cfg, "rc:qp_min",
                venc_mpi_attr->qp_min ? venc_mpi_attr->qp_min : 0);
            mpp_enc_cfg_set_s32(
                cfg, "rc:qp_max_i",
                venc_mpi_attr->qp_max_i ? venc_mpi_attr->qp_max_i : 127);
            mpp_enc_cfg_set_s32(
                cfg, "rc:qp_min_i",
                venc_mpi_attr->qp_min_i ? venc_mpi_attr->qp_min_i : 0);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 6);
        } break;
        case MPP_VIDEO_CodingMJPEG: {
            /* jpeg use special codec config to control qtable */
            mpp_enc_cfg_set_s32(
                cfg, "jpeg:q_factor",
                venc_mpi_attr->qp_init ? venc_mpi_attr->qp_init : 80);
            mpp_enc_cfg_set_s32(
                cfg, "jpeg:qf_max",
                venc_mpi_attr->qp_max ? venc_mpi_attr->qp_max : 99);
            mpp_enc_cfg_set_s32(
                cfg, "jpeg:qf_min",
                venc_mpi_attr->qp_min ? venc_mpi_attr->qp_min : 1);
            ALOGD("venc_mpi_attr->qp_init %d", venc_mpi_attr->qp_init);
            ALOGD("venc_mpi_attr->qp_max %d", venc_mpi_attr->qp_max);
            ALOGD("venc_mpi_attr->qp_min %d", venc_mpi_attr->qp_min);
        } break;
        default: {
        } break;
    }

    /* setup codec  */
    mpp_enc_cfg_set_s32(cfg, "codec:type", venc_mpi_attr->type);
    ALOGD("venc_mpi_attr->type %d", venc_mpi_attr->type);

    switch (venc_mpi_attr->type) {
        case MPP_VIDEO_CodingAVC: {
            RK_U32 constraint_set;

            /*
             * H.264 profile_idc parameter
             * 66  - Baseline profile
             * 77  - Main profile
             * 100 - High profile
             */
            mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);
            /*
             * H.264 level_idc parameter
             * 10 / 11 / 12 / 13    - qcif@15fps / cif@7.5fps / cif@15fps / cif@30fps
             * 20 / 21 / 22         - cif@30fps / half-D1@@25fps / D1@12.5fps
             * 30 / 31 / 32         - D1@25fps / 720p@30fps / 720p@60fps
             * 40 / 41 / 42         - 1080p@30fps / 1080p@30fps / 1080p@60fps
             * 50 / 51 / 52         - 4K@30fps
             */
            mpp_enc_cfg_set_s32(cfg, "h264:level", 40);
            mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
            mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
            mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);

            mpp_env_get_u32("constraint_set", &constraint_set, 0);
            if (constraint_set & 0x3f0000)
                mpp_enc_cfg_set_s32(cfg, "h264:constraint_set", constraint_set);
        } break;
        case MPP_VIDEO_CodingHEVC:
        case MPP_VIDEO_CodingMJPEG:
        case MPP_VIDEO_CodingVP8: {
        } break;
        default: {
            ALOGE("unsupport encoder coding type %d", venc_mpi_attr->type);
        } break;
    }
    ALOGD("split_mode");
    venc_mpi_attr->split_mode = 0;
    venc_mpi_attr->split_arg = 0;
    venc_mpi_attr->split_out = 0;

    mpp_env_get_u32("split_mode", &venc_mpi_attr->split_mode,
                    MPP_ENC_SPLIT_NONE);
    mpp_env_get_u32("split_arg", &venc_mpi_attr->split_arg, 0);
    mpp_env_get_u32("split_out", &venc_mpi_attr->split_out, 0);

    if (venc_mpi_attr->split_mode) {
        mpp_enc_cfg_set_s32(cfg, "split:mode", venc_mpi_attr->split_mode);
        mpp_enc_cfg_set_s32(cfg, "split:arg", venc_mpi_attr->split_arg);
        mpp_enc_cfg_set_s32(cfg, "split:out", venc_mpi_attr->split_out);
    }
    ALOGD("mirroring");
    mpp_env_get_u32("mirroring", &mirroring, 0);
    mpp_env_get_u32("rotation", &rotation, 0);
    mpp_env_get_u32("flip", &flip, 0);

    mpp_enc_cfg_set_s32(cfg, "prep:mirroring", mirroring);
    mpp_enc_cfg_set_s32(cfg, "prep:rotation", rotation);
    mpp_enc_cfg_set_s32(cfg, "prep:flip", flip);
    ALOGD("mirroring %d", mirroring);
    ALOGD("rotation %d", rotation);
    ALOGD("flip %d", flip);

    // config gop_len and ref cfg
    mpp_enc_cfg_set_s32(cfg, "rc:gop",
                        venc_mpi_attr->gop_len
                            ? venc_mpi_attr->gop_len
                            : venc_mpi_attr->fps_out_num * 2);

    ALOGD("gop %d", venc_mpi_attr->gop_len ? venc_mpi_attr->gop_len
                                           : venc_mpi_attr->fps_out_num * 2);

    mpp_env_get_u32("gop_mode", &gop_mode, gop_mode);

    ALOGD("gop_mode %d", gop_mode);

    ALOGD("gop_len %d", venc_mpi_attr->gop_len);

    ALOGD("vi_len %d", venc_mpi_attr->vi_len);

    if (gop_mode) {
        mpp_enc_ref_cfg_init(&ref);

        if (venc_mpi_attr->gop_mode < 4)
            mpi_enc_gen_ref_cfg(ref, gop_mode);
        else
            mpi_enc_gen_smart_gop_ref_cfg(ref, venc_mpi_attr->gop_len,
                                          venc_mpi_attr->vi_len);

        mpp_enc_cfg_set_ptr(cfg, "rc:ref_cfg", ref);
    }
    ALOGD("control MPP_ENC_SET_CFG, ctx %p, mpi %p, control %p", ctx, mpi,
          mpi->control);
    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret) {
        ALOGE("mpi control enc set cfg failed ret %d", ret);
        goto RET;
    }

    ALOGD("ref %p", ref);

    if (ref) mpp_enc_ref_cfg_deinit(&ref);

    /* optional */
    {
        RK_U32 sei_mode;
        ALOGD("sei_mode");
        mpp_env_get_u32("sei_mode", &sei_mode, MPP_ENC_SEI_MODE_ONE_FRAME);
        venc_mpi_attr->sei_mode = (MppEncSeiMode)sei_mode;
        ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &venc_mpi_attr->sei_mode);
        if (ret) {
            ALOGE("mpi control enc set sei cfg failed ret %d", ret);
            goto RET;
        }
    }

    if (venc_mpi_attr->type == MPP_VIDEO_CodingAVC ||
        venc_mpi_attr->type == MPP_VIDEO_CodingHEVC) {
        venc_mpi_attr->header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE,
                           &venc_mpi_attr->header_mode);
        if (ret) {
            ALOGE("mpi control enc set header mode failed ret %d", ret);
            goto RET;
        }
    }
    ALOGD("osd_enable");
    /* setup test mode by env */
    mpp_env_get_u32("osd_enable", &venc_mpi_attr->osd_enable, 0);
    mpp_env_get_u32("osd_mode", &venc_mpi_attr->osd_mode,
                    MPP_ENC_OSD_PLT_TYPE_DEFAULT);
    ALOGD("roi_enable");
    mpp_env_get_u32("roi_enable", &venc_mpi_attr->roi_enable, 0);
    ALOGD("user_data_enable");
    mpp_env_get_u32("user_data_enable", &venc_mpi_attr->user_data_enable, 0);

    if (venc_mpi_attr->roi_enable) {
        mpp_enc_roi_init(&venc_mpi_attr->roi_ctx, venc_mpi_attr->width,
                         venc_mpi_attr->height, venc_mpi_attr->type, 4);
        mpp_assert(venc_mpi_attr->roi_ctx);
    }
RET:
    return ret;
}

MPP_RET MppEncoder::venc_put_src_imge(int v4l2Index) {
    MPP_RET ret;
    MppFrame frame = NULL;

    VENC_MPI_ATTR *p = &venc_mpi_attr;

    ALOGI("%s   ctx: %p", __func__, p->ctx);

    ret = mpp_frame_init(&frame);
    if (ret) {
        ALOGE("mpp_frame_init failed");
        return ret;
    }

    mpp_frame_set_width(frame, p->width);
    mpp_frame_set_height(frame, p->height);
    mpp_frame_set_hor_stride(frame, p->hor_stride);
    mpp_frame_set_ver_stride(frame, p->ver_stride);
    mpp_frame_set_fmt(frame, p->fmt);
    mpp_frame_set_eos(frame, p->frm_eos);

    mpp_frame_set_buffer(frame, mppBuffer[v4l2Index]);

    ret = p->mpi->encode_put_frame(p->ctx, frame);
    if (ret) {
        ALOGE("encode put frame failed");
        mpp_frame_deinit(&frame);
        return ret;
    }

    mpp_frame_deinit(&frame);

    return ret;
}

MPP_RET MppEncoder::venc_get_frame(RK_U8 *frame_buf, size_t *frame_len) {
    MPP_RET ret;
    MppPacket packet = NULL;
    void *ptr;
    size_t len;

    VENC_MPI_ATTR *p = &venc_mpi_attr;

    ret = p->mpi->encode_get_packet(p->ctx, &packet);
    if (ret) {
        ALOGE("chn encode get packet failed");
        return ret;
    }

    ptr = mpp_packet_get_pos(packet);
    len = mpp_packet_get_length(packet);
    if (len > *frame_len) {
        ALOGE("frame_buf is too small, frame_len: %zu < len: %zu", *frame_len,
              len);
        return MPP_NOK;
    }
    ALOGI("%s   frame_buf: %p frame_len: %lu ptr: %p len: %lu", __func__, frame_buf, *frame_len, ptr, len);
    memcpy(frame_buf, ptr, len);
    *frame_len = len;
    mpp_packet_deinit(&packet);

    return MPP_OK;
}

MPP_RET MppEncoder::venc_deinit() {
    VENC_MPI_ATTR *p = &venc_mpi_attr;

    if (p->ctx) {
        mpp_destroy(p->ctx);
        p->ctx = NULL;
    }

    if (p->cfg) {
        mpp_enc_cfg_deinit(p->cfg);
        p->cfg = NULL;
    }

    if (p->frm_buf) {
        mpp_buffer_put(p->frm_buf);
        p->frm_buf = NULL;
    }

    if (p->pkt_buf) {
        mpp_buffer_put(p->pkt_buf);
        p->pkt_buf = NULL;
    }

    if (p->md_info) {
        mpp_buffer_put(p->md_info);
        p->md_info = NULL;
    }

    if (p->buf_grp) {
        mpp_buffer_group_put(p->buf_grp);
        p->buf_grp = NULL;
    }

    for (int i = 0; i < V4L2_BUFFER_COUNT; i++) {
        mpp_buffer_put(mppBuffer[i]);
    }

    return MPP_OK;
}