//
// Created by Charlie on 2024/7/31.
//

#ifndef CAPTUREENCODER_MPI_ENC_H
#define CAPTUREENCODER_MPI_ENC_H

#include <string.h>
#include "mpp_common.h"
#include "mpp_debug.h"
#include "mpp_env.h"
#include "mpp_mem.h"
#include "mpp_rc_api.h"
#include "rk_mpi.h"
#include "rk_type.h"
#include "utils.h"
#include "mpp_enc_roi_utils.h"
#include "mpi_enc_utils.h"
#include <jni.h>
#include "JNIEnvUtil.h"

#define V4L2_BUFFER_COUNT 4

typedef struct VENC_MPI_ATTR {
    MppCtx ctx;
    MppApi *mpi;
    RK_S32 chn;

    MppEncCfg cfg;
    MppEncOSDPltCfg osd_plt_cfg;
    MppEncOSDPlt osd_plt;
    MppEncOSDData osd_data;
    RoiRegionCfg roi_region;
    MppEncROICfg roi_cfg;

    MppBufferGroup buf_grp;
    MppBuffer frm_buf;
    MppBuffer pkt_buf;
    MppBuffer md_info;
    MppEncSeiMode sei_mode;
    MppEncHeaderMode header_mode;

    // resources
    size_t header_size;
    size_t frame_size;
    size_t mdinfo_size;
    /* NOTE: packet buffer may overflow */
    size_t packet_size;

    RK_U32 width;
    RK_U32 height;
    RK_U32 hor_stride;
    RK_U32 ver_stride;
    MppFrameFormat fmt;
    MppCodingType type;
    MppEncRoiCtx roi_ctx;

    RK_U32 osd_enable;
    RK_U32 osd_mode;
    RK_U32 split_mode;
    RK_U32 split_arg;
    RK_U32 split_out;

    RK_U32 user_data_enable;
    RK_U32 roi_enable;

    // rate control runtime parameter
    RK_S32 fps_in_flex;
    RK_S32 fps_in_den;
    RK_S32 fps_in_num;
    RK_S32 fps_out_flex;
    RK_S32 fps_out_den;
    RK_S32 fps_out_num;
    RK_S32 bps;
    RK_S32 bps_max;
    RK_S32 bps_min;
    RK_S32 rc_mode;
    RK_S32 gop_mode;
    RK_S32 gop_len;
    RK_S32 vi_len;
    RK_S32 scene_mode;
    /* -qc */
    RK_S32 qp_init;
    RK_S32 qp_min;
    RK_S32 qp_max;
    RK_S32 qp_min_i;
    RK_S32 qp_max_i;

    /* -fqc */
    RK_S32 fqp_min_i;
    RK_S32 fqp_min_p;
    RK_S32 fqp_max_i;
    RK_S32 fqp_max_p;

    RK_S32 frm_cnt_in;
    RK_U32 frm_eos;
    RK_U32 pkt_eos;

    RK_U32 frame_count;
} VENC_MPI_ATTR_t;

typedef struct VENC_ATTR {
    MppCodingType type;
    MppFrameFormat format;
    RK_U32 width;
    RK_U32 height;

    /* -rc */
    RK_S32 rc_mode;

    /* -bps */
    RK_S32 bps_target;
    RK_S32 bps_max;
    RK_S32 bps_min;

    /* -fps */
    RK_S32 fps_in_flex;
    RK_S32 fps_in_num;
    RK_S32 fps_in_den;
    RK_S32 fps_out_flex;
    RK_S32 fps_out_num;
    RK_S32 fps_out_den;

    /* -qc */
    RK_S32 qp_init;
    RK_S32 qp_min;
    RK_S32 qp_max;
    RK_S32 qp_min_i;
    RK_S32 qp_max_i;

    /* -g gop mode */
    RK_S32 gop_mode;
    RK_S32 gop_len;
    RK_S32 vi_len;

} VENC_ATTR_t;

class MppEncoder {
public:
    MppEncoder();

    ~MppEncoder();

    MPP_RET venc_init(RK_S32 chn, VENC_ATTR_t *venc_attr, v4l2Buffer* buffer, int buf_len);

    MPP_RET venc_put_src_imge(int v4l2Index);

    MPP_RET venc_get_frame(RK_U8 *frame_buf, size_t *frame_len);

    MPP_RET venc_deinit();

private:
    MPP_RET venc_mpi_init(VENC_MPI_ATTR *venc_mpi_attr, VENC_ATTR *venc_attr);

    MPP_RET venc_mpp_cfg_setup(VENC_MPI_ATTR *venc_mpi_attr);

    VENC_MPI_ATTR venc_mpi_attr;

    MppBuffer mppBuffer[V4L2_BUFFER_COUNT];
};

#endif  // CAPTUREENCODER_MPI_ENC_H
