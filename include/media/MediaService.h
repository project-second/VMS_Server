#ifndef __MEDIASERVICE_H__
#define __MEDIASERVICE_H__

#include "CameraManager.h"
#include "RtspRelayServer.h"
#include "OnvifProvisioningRepository.h"

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct MediaRecordingOptions {
    std::string recordRootDir = "recordings/onvif";
    int splitSeconds = 5;
};

class MediaService {
private:
    CameraManager& cameraManager;
    RtspRelayServer& relayServer;
    OnvifProvisioningRepository& repository;
    std::mutex mutex_;
    std::unordered_set<std::string> registeredCameraIds_;
    MediaRecordingOptions recordingOptions_;

public:
    MediaService(
        CameraManager& cm,
        RtspRelayServer& relay,
        OnvifProvisioningRepository& rp,
        MediaRecordingOptions recordingOptions = {});
    ~MediaService();

    void addCamera(const std::vector<ProvisioningResult>& results);
};

#endif // __MEDIASERVICE_H__
