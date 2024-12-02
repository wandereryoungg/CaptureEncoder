#ifndef CAPTUREENCODER_RGACROPSCALE_H
#define CAPTUREENCODER_RGACROPSCALE_H

class RgaCropScale {
public:
    static void convertFormat(int srcWidth, int srcHeight, int srcFd,
                              void* srcAddr, int srcFormat, int dstWidth,
                              int dstHeight, int dstFd, void* dstAddr,
                              int dstFormat);
};

#endif  // CAPTUREENCODER_RGACROPSCALE_H
