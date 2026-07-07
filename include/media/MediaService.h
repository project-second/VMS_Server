#ifndef __MEDIASERVICE_H__
#define __MEDIASERVICE_H__

#include "CameraManager.h"
#include "OnvifProvisioningRepository.h"

class MediaService {
private:
    CameraManager& cameraManager;
    OnvifProvisioningRepository& repository;

public:
    MediaService(CameraManager& cm, OnvifProvisioningRepository& rp);
    ~MediaService();

    void addCamera(std::vector<ProvisioningResult> results);
};

#endif // __MEDIASERVICE_H__