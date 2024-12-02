//
// Created by Charlie on 2024/7/29.
//

#ifndef CAPTUREENCODER_IPROCESSDONELISTENER_H
#define CAPTUREENCODER_IPROCESSDONELISTENER_H

#include "IProcessUnit.h"

class IProcessDoneListener {
public:

    IProcessDoneListener() {}

    virtual ~IProcessDoneListener() {}

    virtual void notifyProcessDone(std::shared_ptr<IProcessUnit::ProcessBuf>& processBuf) = 0;

};


#endif //CAPTUREENCODER_IPROCESSDONELISTENER_H
