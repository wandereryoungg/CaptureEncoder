//
// Created by Charlie on 2024/7/24.
//

#ifndef CAPTUREENCODER_PREVIEWUNIT_H
#define CAPTUREENCODER_PREVIEWUNIT_H

#include <system/window.h>
#include <utils/RefBase.h>
#include <utils/Thread.h>

#include "IProcessDoneListener.h"
#include "IProcessUnit.h"

class PreviewUnit : public IProcessUnit {
public:
    PreviewUnit(IProcessDoneListener* processDoneListener, ANativeWindow* nativeWindow, int width, int height);

    ~PreviewUnit();

    int32_t processBuffer(
        std::shared_ptr<ProcessBuf>& processBuf) override;

    void waitForNextRequest(std::shared_ptr<ProcessBuf>* out) override;

private:
    IProcessDoneListener* mIProcessDoneListener;

    virtual bool threadLoop();

    virtual status_t readyToRun();

    ANativeWindow* mNativeWindow = nullptr;

    int mWidth;

    int mHeight;
};

#endif  // CAPTUREENCODER_PREVIEWUNIT_H
