//
// Created by Charlie on 2024/8/1.
//

#ifndef CAPTUREENCODER_MPPCHANNEL_H
#define CAPTUREENCODER_MPPCHANNEL_H

#include <vector>
#include <mutex>

class MppChannel {
public:
    static MppChannel& getInstance();

    MppChannel();

    ~MppChannel();

    int getChannel();

    int releaseChannel(int channelId);

private:
    static MppChannel sInstance;

    struct MppChn {
        int channelId;
        bool inUse;
    };

    std::vector<MppChn> mChannels;

    std::mutex mChannelLock;

};



#endif //CAPTUREENCODER_MPPCHANNEL_H
