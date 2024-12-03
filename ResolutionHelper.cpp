//
// Created by Charlie on 2024/11/5.
//
#define LOG_TAG "ResolutionHelper"

#include "ResolutionHelper.h"
#include <log/log.h>
#include <dirent.h>
#include "v4l2device.h"
#include "MediaEntity.h"
#include "RKISP2CameraCapInfo.h"

namespace android {
namespace camera2 {

std::string ResolutionHelper::getSensorMediaDevice(int cameraId) {

	const rkisp2::RKISP2CameraCapInfo* cap = (rkisp2::RKISP2CameraCapInfo*)PlatformData::getCameraCapInfo(cameraId);
	string sensorName = cap->getSensorName();
	string moduleIdStr = cap->mModuleIndexStr;

	const std::vector<struct SensorDriverDescriptor>& sensorInfo = PlatformData::getCameraHWInfo()->mSensorInfo;
	for (auto it = sensorInfo.begin(); it != sensorInfo.end(); ++it) {
		if ((*it).mSensorName == sensorName && (*it).mModuleIndexStr == moduleIdStr) {
		    ALOGI("%s   found cameraId: %d   media_ctl: %s", __func__, cameraId, it->mParentMediaDev.c_str());
		    return (*it).mParentMediaDev;
		}

	}

	ALOGE("%s   Can't get SensorMediaDevice cameraId: %d", __func__, cameraId);
	return "none";
}

std::string ResolutionHelper::getImgMediaDevice(int cameraId, std::shared_ptr<MediaController> sensorMediaCtl) {
	std::vector<std::string> mediaDevicePaths;
	std::vector<std::string> mediaDeviceNames{ "rkisp1","rkisp0" };

	mediaDevicePaths = getMediaDeviceByModuleNames(mediaDeviceNames);

	if (mediaDevicePaths.size() > 0) {
		for (auto it : mediaDevicePaths) {
			std::shared_ptr<MediaController> mediaController = std::make_shared<MediaController>(it.c_str());
			//init twice, disable this
			if (mediaController->init() != NO_ERROR) {
				ALOGE("%s   Error initializing Media Controller: %s", __func__, it.c_str());
				continue;
			}

			media_device_info info;
			int ret = sensorMediaCtl->getMediaDevInfo(info);
			if (ret != OK) {
				ALOGE("Cannot get media device information.");
			}
			ALOGI("%s   sensorMediaCtl info.model: %s", __func__, info.model);

			struct media_entity_desc entity;
			status_t status = NO_ERROR;
			status = mediaController->findMediaEntityByName(info.model, entity);
			if (status == NO_ERROR) {
				// found entity by name
				ALOGI("%s   found entity by name ,entity.info.name:%s", __func__, info.model);
				return it;
			}
		}
	}
	ALOGE("%s only one media", __func__);

	return getSensorMediaDevice(cameraId);
}

std::vector<std::string> ResolutionHelper::getMediaDeviceByModuleNames(const std::vector<std::string>& moduleNames) {

	const char* MEDIADEVICES = "media";
	const char* DEVICE_PATH = "/dev/";

	std::vector<std::string> mediaDevicePath;
	DIR* dir;
	dirent* dirEnt;

	std::vector<std::string> candidates;

	candidates.clear();
	if ((dir = opendir(DEVICE_PATH)) != nullptr) {
		while ((dirEnt = readdir(dir)) != nullptr) {
			std::string candidatePath = dirEnt->d_name;
			std::size_t pos = candidatePath.find(MEDIADEVICES);
			if (pos != std::string::npos) {
				ALOGI("%s   Found media device candidate: %s", __func__, candidatePath.c_str());
				std::string found_one = DEVICE_PATH;
				found_one += candidatePath;
				candidates.push_back(found_one);
			}
		}
		closedir(dir);
	} else {
		ALOGE("%s   Failed to open directory: %s", __func__, DEVICE_PATH);
	}

	status_t retVal = NO_ERROR;
	// let media0 place before media1
	std::sort(candidates.begin(), candidates.end());
	for (const auto& candidate : candidates) {
		MediaController controller(candidate.c_str());
		retVal = controller.init();

		// We may run into devices that this HAL won't use -> skip to next
		if (retVal == PERMISSION_DENIED) {
			ALOGE("%s   Not enough permissions to access %s.", __func__, candidate.c_str());
			continue;
		}

		media_device_info info;
		int ret = controller.getMediaDevInfo(info);
		if (ret != OK) {
			ALOGE("%s   Cannot get media device information.", __func__);
			return mediaDevicePath;
		}

		for (auto it : moduleNames) {
			ALOGI("%s   Target name: %s candidate name: %s ", __func__, it.c_str(), info.model);
			if (strncmp(info.model, it.c_str(),
				MIN(sizeof(info.model),
					it.size())) == 0) {
				ALOGI("%s   Found device(%s) that matches: %s", __func__, candidate.c_str(), it.c_str());
				mediaDevicePath.push_back(candidate);
			}
		}
	}

	return mediaDevicePath;
}

void ResolutionHelper::updateResolution(int cameraId, const char* entityName, int32_t& width, int32_t& height) {
    ALOGI("%s   entityName: %s", __func__, entityName);
    status_t status = NO_ERROR;

    std::string sensorMediaDevice = getSensorMediaDevice(cameraId);
    std::shared_ptr<MediaController> mediaCtl = std::make_shared<MediaController>(sensorMediaDevice.c_str());
	status = mediaCtl->init();
	if (status != NO_ERROR) {
		ALOGE("%s   error initializing sensor media controller", __func__);
		return;
	}

	std::string imgMediaDevice = getImgMediaDevice(cameraId, mediaCtl);
    std::shared_ptr<MediaController> imgMediaCtl = std::make_shared<MediaController>(imgMediaDevice.c_str());
	status = imgMediaCtl->init();
	if (status != NO_ERROR) {
		ALOGE("%s   error initializing img media controller", __func__);
		return;
	}

    std::shared_ptr<MediaEntity> entity = nullptr;
    std::shared_ptr<V4L2VideoNode> videoNode = nullptr;

    status = imgMediaCtl->getMediaEntity(entity, "stream_cif_mipi_id0");
    if (status != NO_ERROR) {
        ALOGE("%s   Getting MediaEntity stream_cif_mipi_id0 failed", __func__);
        return;
    }
    status = entity->getDevice((std::shared_ptr<V4L2DeviceBase>&) videoNode);
    if (status != NO_ERROR) {
        ALOGE("%s   Error opening devicestream_cif_mipi_id0", __func__);
        return;
    }
    V4L2Format format;
    status = videoNode->getFormat(format);
    if (status != NO_ERROR) {
        ALOGE("%s   Error getFormat", __func__);
        return;
    }
    width = format.width();
    height = format.height();
    ALOGI("%s   width: %d height: %d", __func__, width, height);
}



}
}
