#include "MediaService.h"

#include "LogSync.h"

MediaService::MediaService(CameraManager& cm, OnvifProvisioningRepository& rp) 
    : cameraManager{cm}, repository{rp} {}

MediaService::~MediaService() {}

void MediaService::addCamera(std::vector<ProvisioningResult> results) {
    if (!results.empty() && !repository.upsert(results)) {
        LogSync::error("[Provisioning] failed to upsert provisioning results");
    }

    // for (auto result : results) {
    //     CameraConfig config;

    //     config.camera_id = "";

    //     for (auto profile : result.profiles) {
    //         profile.profile.
    //     }

        // config.rtsp_uri = result.profiles
}