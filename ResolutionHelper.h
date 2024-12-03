//
// Created by Charlie on 2024/11/5.
//

#ifndef CAMERA_RESOLUTIONHELPER_H
#define CAMERA_RESOLUTIONHELPER_H

#include <string>
#include <vector>
#include "PlatformData.h"

namespace android {
namespace camera2 {

class ResolutionHelper {
public:
static std::string getSensorMediaDevice(int cameraId);
static void updateResolution(int cameraId, const char* entityName, int32_t& width, int32_t& height);
static std::string getImgMediaDevice(int cameraId, std::shared_ptr<MediaController> sensorMediaCtl);
static std::vector<std::string> getMediaDeviceByModuleNames(const std::vector<std::string>& moduleName);
};


}
}
#endif //CAMERA_RESOLUTIONHELPER_H
