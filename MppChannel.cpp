//
// Created by Charlie on 2024/8/1.
//

#include "MppChannel.h"
#include <log/log.h>

#define MAX_MPP_CHANNEL_NUM 6

MppChannel MppChannel::sInstance;

MppChannel& MppChannel::getInstance() {
	return sInstance;
}

MppChannel::MppChannel() {
    std::lock_guard<std::mutex> lk(mChannelLock);
    ALOGI("%s   MppChannel: %p", __func__, this);
    for (int i = 0; i < MAX_MPP_CHANNEL_NUM; i++) {
        struct MppChn channel{
            .channelId = i,
            .inUse = false
        };
        mChannels.push_back(channel);
    }
}

int MppChannel::getChannel() {
    std::lock_guard<std::mutex> lk(mChannelLock);
    for (auto iter = mChannels.begin(); iter != mChannels.end(); iter++) {
        bool inUse = iter->inUse;
        if (inUse == false) {
            int channelId = iter->channelId;
            iter->inUse = true;
            ALOGI("%s   channelId: %d", __func__, channelId);
            return channelId;
        }
    }
    ALOGI("%s   failed", __func__);
    return -1;
}

int MppChannel::releaseChannel(int channelId) {
    std::lock_guard<std::mutex> lk(mChannelLock);
    for (auto iter = mChannels.begin(); iter != mChannels.end(); iter++) {
        if (channelId == iter->channelId) {
            iter->inUse = false;
            ALOGI("%s   channelId: %d okay", __func__, channelId);
            return channelId;
        }
    }
    ALOGI("%s   failed", __func__);
    return -1;
}

MppChannel::~MppChannel() {
    std::lock_guard<std::mutex> lk(mChannelLock);
    ALOGI("%s   MppChannel: %p", __func__, this);
    mChannels.clear();
}


