#include "VmsDeviceService.h"

#include "LogSync.h"
#include "OnvifPtzClient.h"
#include "SecretCodec.h"
#include "StringHelpers.h"
#include "VmsDeviceRepository.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {

VmsDeviceRepository& defaultDeviceRepository() {
    static VmsDeviceRepository repository;
    return repository;
}

OnvifPtzClient& defaultPtzClient() {
    static OnvifPtzClient client;
    return client;
}

std::string healthName(std::int64_t onlineState, std::int64_t videoState) {
    if (onlineState <= 0) {
        return "DOWN";
    }
    if (videoState <= 0) {
        return "WARN";
    }
    return "OK";
}

VmsDeviceService::DeviceItem toDeviceItem(const VmsDeviceRepository::DeviceRecord& record) {
    VmsDeviceService::DeviceItem item;
    item.deviceId = record.deviceId;
    item.type = record.type;
    item.ip = record.addr;
    item.name = record.name;
    item.model = record.model;
    item.online = record.onlineState > 0;
    item.health = healthName(record.onlineState, record.videoState);
    item.channelCount = record.channelCount;
    return item;
}

VmsDeviceService::ChannelItem toChannelItem(const VmsDeviceRepository::ChannelRecord& record) {
    VmsDeviceService::ChannelItem item;
    item.channelId = record.channelId;
    item.channelNo = record.channelNo;
    item.name = record.name;
    item.videoCodec = record.videoCodec;
    item.audioCodec = record.audioCodec;
    item.rtsp = record.rtsp;
    return item;
}

VmsDeviceService::TourItem toTourItem(const VmsDeviceRepository::TourItemRecord& record) {
    VmsDeviceService::TourItem item;
    item.tourItemId = record.tourItemId;
    item.presetId = record.presetId;
    item.presetToken = record.presetToken;
    item.presetName = record.presetName;
    item.seqNo = record.seqNo;
    item.staySeconds = record.staySeconds;
    item.moveSpeed = record.moveSpeed;
    return item;
}

VmsDeviceService::TourItemResult toTourResult(
    const VmsDeviceRepository::TourRecord& record,
    const std::vector<VmsDeviceRepository::TourItemRecord>& itemRecords = {}) {
    VmsDeviceService::TourItemResult item;
    item.tourId = record.tourId;
    item.tourToken = record.tourToken;
    item.tourName = record.tourName;
    item.repeatMode = record.repeatMode;
    item.isEnabled = record.isEnabled;
    item.itemCount = record.itemCount;
    item.items.reserve(itemRecords.size());
    for (const auto& itemRecord : itemRecords) {
        item.items.emplace_back(toTourItem(itemRecord));
    }
    if (!item.items.empty()) {
        item.itemCount = static_cast<std::int64_t>(item.items.size());
    }
    return item;
}

} // namespace

VmsDeviceService::VmsDeviceService()
    : deviceRepository(defaultDeviceRepository()), ptzClient(defaultPtzClient()) {}

VmsDeviceService::VmsDeviceService(VmsDeviceRepository& deviceRepository)
    : deviceRepository(deviceRepository), ptzClient(defaultPtzClient()) {}

VmsDeviceService::VmsDeviceService(VmsDeviceRepository& deviceRepository, OnvifPtzClient& ptzClient)
    : deviceRepository(deviceRepository), ptzClient(ptzClient) {}

VmsDeviceService::~VmsDeviceService() {}

VmsDeviceService::DeviceListResult VmsDeviceService::getDeviceList(int page, int size) {
    const int normalizedPage = std::max(1, page);
    const int normalizedSize = std::max(1, std::min(size, 100));

    DeviceListResult result;
    result.page = normalizedPage;
    result.size = normalizedSize;

    const auto devices = deviceRepository.listDevices(
        normalizedSize,
        (normalizedPage - 1) * normalizedSize);
    for (const auto& device : devices) {
        result.items.emplace_back(toDeviceItem(device));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsDeviceService::ChangeNameResult VmsDeviceService::changeDeviceName(
    std::int64_t deviceId,
    const std::string& name) {
    ChangeNameResult result;
    result.deviceId = deviceId;
    result.name = trim(name);

    if (result.name.empty() || result.name.size() > 128) {
        result.status = MutationStatus::InvalidArgument;
        return result;
    }

    if (!deviceRepository.findDeviceById(deviceId)) {
        result.status = MutationStatus::NotFound;
        return result;
    }

    if (!deviceRepository.updateDeviceName(deviceId, result.name)) {
        LogSync::error("[device.changeName] update failed for device_id=", deviceId);
        result.status = MutationStatus::InternalError;
        return result;
    }

    result.status = MutationStatus::Success;
    return result;
}

VmsDeviceService::ChannelListResult VmsDeviceService::getDeviceChannelList(std::int64_t deviceId) {
    ChannelListResult result;
    result.deviceId = deviceId;

    if (!deviceRepository.findDeviceById(deviceId)) {
        result.status = ListStatus::NotFound;
        return result;
    }

    const auto channels = deviceRepository.listDeviceChannels(deviceId);
    for (const auto& channel : channels) {
        result.items.emplace_back(toChannelItem(channel));
    }

    result.status = ListStatus::Success;
    return result;
}


namespace {

bool validVelocity(float value) {
    return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
}

bool isConfigured(const VmsDeviceRepository::PtzTargetRecord& target) {
    return !trim(target.ptzXAddr).empty() && !trim(target.profileToken).empty();
}

OnvifPtzClientOptions ptzOptionsForTarget(const VmsDeviceRepository::PtzTargetRecord& target) {
    OnvifPtzClientOptions options;
    const std::string username = trim(target.username);
    if (username.empty()) {
        return options;
    }

    options.wsSecurity.enabled = true;
    options.wsSecurity.username = username;
    if (const auto password = decode_stored_secret(target.passwordEnc)) {
        options.wsSecurity.password = *password;
    }
    options.wsSecurity.passwordType = OnvifWsSecurityPasswordType::Digest;
    return options;
}

OnvifPtzClient ptzClientForTarget(const VmsDeviceRepository::PtzTargetRecord& target) {
    return OnvifPtzClient(ptzOptionsForTarget(target));
}

std::mutex& ptzCommandLockMapMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::int64_t, std::shared_ptr<std::timed_mutex>>& ptzCommandLocks() {
    static std::unordered_map<std::int64_t, std::shared_ptr<std::timed_mutex>> locks;
    return locks;
}

std::shared_ptr<std::timed_mutex> ptzCommandLockForDevice(std::int64_t deviceId) {
    std::lock_guard<std::mutex> guard(ptzCommandLockMapMutex());
    auto& lock = ptzCommandLocks()[deviceId];
    if (!lock) {
        lock = std::make_shared<std::timed_mutex>();
    }
    return lock;
}

std::optional<std::unique_lock<std::timed_mutex>> tryLockPtzCommand(
    const VmsDeviceRepository::PtzTargetRecord& target,
    const char* operationName) {
    constexpr auto kPtzCommandLockTimeout = std::chrono::milliseconds(200);

    auto mutex = ptzCommandLockForDevice(target.deviceId);
    std::unique_lock<std::timed_mutex> lock(*mutex, std::defer_lock);
    if (!lock.try_lock_for(kPtzCommandLockTimeout)) {
        LogSync::error(
            "[device.ptz] command busy: operation=", operationName,
            " device_id=", target.deviceId,
            " channel_id=", target.channelId);
        return std::nullopt;
    }
    return std::move(lock);
}


bool isTourSupported(const VmsDeviceRepository::PtzTargetRecord& target) {
    return isConfigured(target) && (!target.hasCapabilities || target.supportsTour);
}

std::string normalizeRepeatMode(const std::string& repeatMode) {
    std::string mode = trim(repeatMode);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (mode.empty()) {
        mode = "LOOP";
    }
    return mode;
}

bool validRepeatMode(const std::string& repeatMode) {
    return repeatMode == "LOOP" || repeatMode == "ONCE";
}

bool validTourName(const std::string& name) {
    return !name.empty() && name.size() <= 128;
}

bool validTourInputItem(const VmsDeviceService::TourItemInput& item) {
    return item.presetId > 0
        && item.seqNo > 0
        && item.staySeconds >= 0
        && item.staySeconds <= 86400
        && std::isfinite(item.moveSpeed)
        && item.moveSpeed >= 0.0
        && item.moveSpeed <= 100.0;
}

std::string xsdDurationSeconds(std::int64_t seconds) {
    return "PT" + std::to_string(std::max<std::int64_t>(0, seconds)) + "S";
}

OnvifPtzPresetTour makeOnvifTour(
    const std::string& tourToken,
    const VmsDeviceService::TourWriteRequest& request,
    const std::vector<VmsDeviceRepository::TourItemRecord>& items) {
    OnvifPtzPresetTour tour;
    tour.token = tourToken;
    tour.name = request.tourName;
    tour.autoStart = request.isEnabled;
    tour.direction = "Forward";
    if (request.repeatMode == "ONCE") {
        tour.recurringTime = 1;
    }

    tour.spots.reserve(items.size());
    for (const auto& item : items) {
        OnvifPtzPresetTourSpot spot;
        spot.presetToken = item.presetToken;
        spot.stayTime = xsdDurationSeconds(item.staySeconds);
        if (item.moveSpeed > 0.0) {
            const float speed = static_cast<float>(item.moveSpeed / 100.0);
            spot.panSpeed = speed;
            spot.tiltSpeed = speed;
            spot.zoomSpeed = speed;
        }
        tour.spots.emplace_back(std::move(spot));
    }
    return tour;
}

VmsDeviceService::PtzControlStatus toPtzStatus(OnvifPtzOperationStatus status) {
    switch (status) {
    case OnvifPtzOperationStatus::Success:
        return VmsDeviceService::PtzControlStatus::Success;
    case OnvifPtzOperationStatus::InvalidArgument:
        return VmsDeviceService::PtzControlStatus::InvalidArgument;
    case OnvifPtzOperationStatus::Unsupported:
        return VmsDeviceService::PtzControlStatus::Unsupported;
    case OnvifPtzOperationStatus::UpstreamError:
        return VmsDeviceService::PtzControlStatus::UpstreamError;
    }
    return VmsDeviceService::PtzControlStatus::UpstreamError;
}

std::vector<VmsDeviceRepository::TourItemRecord> resolveTourItems(
    VmsDeviceRepository& repository,
    std::int64_t channelId,
    const std::vector<VmsDeviceService::TourItemInput>& inputs,
    bool& missingPreset,
    bool& invalidPreset) {
    missingPreset = false;
    invalidPreset = false;

    std::vector<VmsDeviceRepository::TourItemRecord> items;
    items.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto preset = repository.findChannelPreset(channelId, input.presetId);
        if (!preset) {
            missingPreset = true;
            return {};
        }
        if (trim(preset->presetToken).empty()) {
            invalidPreset = true;
            return {};
        }

        VmsDeviceRepository::TourItemRecord item;
        item.presetId = input.presetId;
        item.presetToken = preset->presetToken;
        item.presetName = preset->presetName;
        item.seqNo = input.seqNo;
        item.staySeconds = input.staySeconds;
        item.moveSpeed = input.moveSpeed;
        items.emplace_back(std::move(item));
    }
    return items;
}

} // namespace

VmsDeviceService::PtzControlResult VmsDeviceService::panChannel(std::int64_t channelId, float speed) {
    if (!validVelocity(speed) || speed == 0.0f) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target) || (target->hasCapabilities && !target->supportsPan)) {
        return {PtzControlStatus::Unsupported};
    }

    auto commandLock = tryLockPtzCommand(*target, "pan");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.continuousMove(target->ptzXAddr, target->profileToken, speed, 0.0f, 0.0f)) {
        LogSync::error("[device.ptz.pan] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::tiltChannel(std::int64_t channelId, float speed) {
    if (!validVelocity(speed) || speed == 0.0f) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target) || (target->hasCapabilities && !target->supportsTilt)) {
        return {PtzControlStatus::Unsupported};
    }

    auto commandLock = tryLockPtzCommand(*target, "tilt");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.continuousMove(target->ptzXAddr, target->profileToken, 0.0f, speed, 0.0f)) {
        LogSync::error("[device.ptz.tilt] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::moveChannelLinear(
    std::int64_t channelId,
    float pan,
    float tilt,
    float zoom) {
    if (!validVelocity(pan) || !validVelocity(tilt) || !validVelocity(zoom)
        || (pan == 0.0f && tilt == 0.0f && zoom == 0.0f)) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)
        || (target->hasCapabilities
            && (!target->supportsLinear
                || (pan != 0.0f && !target->supportsPan)
                || (tilt != 0.0f && !target->supportsTilt)
                || (zoom != 0.0f && !target->supportsZoom)))) {
        return {PtzControlStatus::Unsupported};
    }

    auto commandLock = tryLockPtzCommand(*target, "linear");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.continuousMove(target->ptzXAddr, target->profileToken, pan, tilt, zoom)) {
        LogSync::error("[device.ptz.linear] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::stopChannel(
    std::int64_t channelId,
    bool panTilt,
    bool zoom) {
    if (!panTilt && !zoom) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    auto commandLock = tryLockPtzCommand(*target, "stop");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.stop(target->ptzXAddr, target->profileToken, panTilt, zoom)) {
        LogSync::error("[device.ptz.stop] ONVIF Stop failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PresetListResult VmsDeviceService::getChannelPresetList(std::int64_t channelId) {
    PresetListResult result;
    result.channelId = channelId;

    if (!deviceRepository.findPtzTargetByChannelId(channelId)) {
        result.status = ListStatus::NotFound;
        return result;
    }

    const auto presets = deviceRepository.listChannelPresets(channelId);
    for (const auto& preset : presets) {
        PresetItem item;
        item.presetId = preset.presetId;
        item.presetToken = preset.presetToken;
        item.presetName = preset.presetName;
        item.pan = preset.pan;
        item.tilt = preset.tilt;
        item.zoom = preset.zoom;
        item.linear = preset.linear;
        result.items.emplace_back(std::move(item));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsDeviceService::PtzControlResult VmsDeviceService::setChannelPreset(
    std::int64_t channelId,
    const std::string& presetName) {
    const std::string name = trim(presetName);
    if (name.empty() || name.size() > 128) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    auto commandLock = tryLockPtzCommand(*target, "preset.set");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    const auto presetToken = client.setPreset(target->ptzXAddr, target->profileToken, name);
    if (!presetToken || trim(*presetToken).empty()) {
        LogSync::error("[device.ptz.preset.set] ONVIF SetPreset failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }

    if (!deviceRepository.upsertChannelPreset(channelId, trim(*presetToken), name)) {
        LogSync::error("[device.ptz.preset.set] DB upsert failed for channel_id=", channelId);
        return {PtzControlStatus::InternalError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::goToChannelPreset(
    std::int64_t channelId,
    std::int64_t presetId) {
    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    const auto preset = deviceRepository.findChannelPreset(channelId, presetId);
    if (!preset) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(preset->presetToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "preset.goto");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.gotoPreset(target->ptzXAddr, target->profileToken, preset->presetToken)) {
        LogSync::error("[device.ptz.preset.goto] ONVIF GotoPreset failed for preset_id=", presetId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::updateChannelPreset(
    std::int64_t channelId,
    std::int64_t presetId,
    const std::string& presetName) {
    const std::string name = trim(presetName);
    if (name.empty() || name.size() > 128) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    const auto preset = deviceRepository.findChannelPreset(channelId, presetId);
    if (!preset) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(preset->presetToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "preset.update");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    const auto updatedToken = client.setPreset(
        target->ptzXAddr,
        target->profileToken,
        name,
        preset->presetToken);
    if (!updatedToken) {
        LogSync::error("[device.ptz.preset.update] ONVIF SetPreset failed for preset_id=", presetId);
        return {PtzControlStatus::UpstreamError};
    }

    if (!deviceRepository.updateChannelPresetName(channelId, presetId, name)) {
        LogSync::error("[device.ptz.preset.update] DB update failed for preset_id=", presetId);
        return {PtzControlStatus::InternalError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::deleteChannelPreset(
    std::int64_t channelId,
    std::int64_t presetId) {
    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    const auto preset = deviceRepository.findChannelPreset(channelId, presetId);
    if (!preset) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(preset->presetToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "preset.delete");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    if (!client.removePreset(target->ptzXAddr, target->profileToken, preset->presetToken)) {
        LogSync::error("[device.ptz.preset.delete] ONVIF RemovePreset failed for preset_id=", presetId);
        return {PtzControlStatus::UpstreamError};
    }

    if (!deviceRepository.deleteChannelPreset(channelId, presetId)) {
        LogSync::error("[device.ptz.preset.delete] DB delete failed for preset_id=", presetId);
        return {PtzControlStatus::InternalError};
    }
    return {PtzControlStatus::Success};
}


VmsDeviceService::TourListResult VmsDeviceService::getChannelTourList(std::int64_t channelId) {
    TourListResult result;
    result.channelId = channelId;

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        result.status = ListStatus::NotFound;
        return result;
    }
    if (!isTourSupported(*target)) {
        result.status = ListStatus::Unsupported;
        return result;
    }

    const auto tours = deviceRepository.listChannelTours(channelId);
    result.items.reserve(tours.size());
    for (const auto& tour : tours) {
        result.items.emplace_back(toTourResult(tour));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsDeviceService::TourDetailResult VmsDeviceService::getChannelTourInfo(
    std::int64_t channelId,
    std::int64_t tourId) {
    TourDetailResult result;
    result.channelId = channelId;

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        result.status = ListStatus::NotFound;
        return result;
    }
    if (!isTourSupported(*target)) {
        result.status = ListStatus::Unsupported;
        return result;
    }

    const auto tour = deviceRepository.findChannelTour(channelId, tourId);
    if (!tour) {
        result.status = ListStatus::NotFound;
        return result;
    }

    const auto items = deviceRepository.listChannelTourItems(tourId);
    result.item = toTourResult(*tour, items);
    result.status = ListStatus::Success;
    return result;
}

VmsDeviceService::TourMutationResult VmsDeviceService::createChannelTour(
    std::int64_t channelId,
    const TourWriteRequest& rawRequest) {
    TourWriteRequest request = rawRequest;
    request.tourName = trim(request.tourName);
    request.repeatMode = normalizeRepeatMode(request.repeatMode);
    if (!validTourName(request.tourName) || !validRepeatMode(request.repeatMode)) {
        return {PtzControlStatus::InvalidArgument, std::nullopt};
    }
    for (const auto& item : request.items) {
        if (!validTourInputItem(item)) {
            return {PtzControlStatus::InvalidArgument, std::nullopt};
        }
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound, std::nullopt};
    }
    if (!isTourSupported(*target)) {
        return {PtzControlStatus::Unsupported, std::nullopt};
    }

    bool missingPreset = false;
    bool invalidPreset = false;
    auto itemRecords = resolveTourItems(deviceRepository, channelId, request.items, missingPreset, invalidPreset);
    if (missingPreset) {
        return {PtzControlStatus::NotFound, std::nullopt};
    }
    if (invalidPreset) {
        return {PtzControlStatus::InternalError, std::nullopt};
    }

    auto commandLock = tryLockPtzCommand(*target, "tour.create");
    if (!commandLock) {
        return {PtzControlStatus::Busy, std::nullopt};
    }

    const auto client = ptzClientForTarget(*target);
    const auto tourToken = client.createPresetTour(target->ptzXAddr, target->profileToken);
    if (!tourToken || trim(*tourToken).empty()) {
        LogSync::error("[device.ptz.tour.create] ONVIF CreatePresetTour failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError, std::nullopt};
    }

    if (!itemRecords.empty()) {
        const auto onvifTour = makeOnvifTour(trim(*tourToken), request, itemRecords);
        const auto status = toPtzStatus(client.modifyPresetTour(target->ptzXAddr, target->profileToken, onvifTour));
        if (status != PtzControlStatus::Success) {
            LogSync::error("[device.ptz.tour.create] ONVIF ModifyPresetTour failed for channel_id=", channelId);
            return {status, std::nullopt};
        }
    }

    const auto tour = deviceRepository.upsertChannelTour(
        channelId,
        trim(*tourToken),
        request.tourName,
        request.repeatMode,
        false,
        request.isEnabled);
    if (!tour) {
        LogSync::error("[device.ptz.tour.create] DB upsert failed for channel_id=", channelId);
        return {PtzControlStatus::InternalError, std::nullopt};
    }
    for (auto& item : itemRecords) {
        item.tourId = tour->tourId;
    }
    if (!deviceRepository.replaceChannelTourItems(tour->tourId, itemRecords)) {
        LogSync::error("[device.ptz.tour.create] DB item replace failed for tour_id=", tour->tourId);
        return {PtzControlStatus::InternalError, std::nullopt};
    }

    const auto savedItems = deviceRepository.listChannelTourItems(tour->tourId);
    return {PtzControlStatus::Success, toTourResult(*tour, savedItems)};
}

VmsDeviceService::TourMutationResult VmsDeviceService::updateChannelTour(
    std::int64_t channelId,
    std::int64_t tourId,
    const TourWriteRequest& rawRequest) {
    TourWriteRequest request = rawRequest;
    request.tourName = trim(request.tourName);
    request.repeatMode = normalizeRepeatMode(request.repeatMode);
    if (!validTourName(request.tourName) || !validRepeatMode(request.repeatMode)) {
        return {PtzControlStatus::InvalidArgument, std::nullopt};
    }
    for (const auto& item : request.items) {
        if (!validTourInputItem(item)) {
            return {PtzControlStatus::InvalidArgument, std::nullopt};
        }
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound, std::nullopt};
    }
    if (!isTourSupported(*target)) {
        return {PtzControlStatus::Unsupported, std::nullopt};
    }

    const auto existingTour = deviceRepository.findChannelTour(channelId, tourId);
    if (!existingTour) {
        return {PtzControlStatus::NotFound, std::nullopt};
    }
    if (trim(existingTour->tourToken).empty()) {
        return {PtzControlStatus::InternalError, std::nullopt};
    }

    bool missingPreset = false;
    bool invalidPreset = false;
    auto itemRecords = resolveTourItems(deviceRepository, channelId, request.items, missingPreset, invalidPreset);
    if (missingPreset) {
        return {PtzControlStatus::NotFound, std::nullopt};
    }
    if (invalidPreset) {
        return {PtzControlStatus::InternalError, std::nullopt};
    }

    auto commandLock = tryLockPtzCommand(*target, "tour.update");
    if (!commandLock) {
        return {PtzControlStatus::Busy, std::nullopt};
    }

    const auto client = ptzClientForTarget(*target);
    const auto onvifTour = makeOnvifTour(existingTour->tourToken, request, itemRecords);
    const auto status = toPtzStatus(client.modifyPresetTour(target->ptzXAddr, target->profileToken, onvifTour));
    if (status != PtzControlStatus::Success) {
        LogSync::error("[device.ptz.tour.update] ONVIF ModifyPresetTour failed for tour_id=", tourId);
        return {status, std::nullopt};
    }

    const auto tour = deviceRepository.upsertChannelTour(
        channelId,
        existingTour->tourToken,
        request.tourName,
        request.repeatMode,
        existingTour->scheduleEnabled,
        request.isEnabled);
    if (!tour) {
        LogSync::error("[device.ptz.tour.update] DB upsert failed for tour_id=", tourId);
        return {PtzControlStatus::InternalError, std::nullopt};
    }
    for (auto& item : itemRecords) {
        item.tourId = tour->tourId;
    }
    if (!deviceRepository.replaceChannelTourItems(tour->tourId, itemRecords)) {
        LogSync::error("[device.ptz.tour.update] DB item replace failed for tour_id=", tour->tourId);
        return {PtzControlStatus::InternalError, std::nullopt};
    }

    const auto savedItems = deviceRepository.listChannelTourItems(tour->tourId);
    return {PtzControlStatus::Success, toTourResult(*tour, savedItems)};
}

VmsDeviceService::PtzControlResult VmsDeviceService::startChannelTour(
    std::int64_t channelId,
    std::int64_t tourId) {
    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isTourSupported(*target)) {
        return {PtzControlStatus::Unsupported};
    }
    const auto tour = deviceRepository.findChannelTour(channelId, tourId);
    if (!tour) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(tour->tourToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "tour.start");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    return {toPtzStatus(client.operatePresetTour(
        target->ptzXAddr,
        target->profileToken,
        tour->tourToken,
        OnvifPtzPresetTourOperation::Start))};
}

VmsDeviceService::PtzControlResult VmsDeviceService::stopChannelTour(
    std::int64_t channelId,
    std::int64_t tourId) {
    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isTourSupported(*target)) {
        return {PtzControlStatus::Unsupported};
    }
    const auto tour = deviceRepository.findChannelTour(channelId, tourId);
    if (!tour) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(tour->tourToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "tour.stop");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    return {toPtzStatus(client.operatePresetTour(
        target->ptzXAddr,
        target->profileToken,
        tour->tourToken,
        OnvifPtzPresetTourOperation::Stop))};
}

VmsDeviceService::PtzControlResult VmsDeviceService::deleteChannelTour(
    std::int64_t channelId,
    std::int64_t tourId) {
    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isTourSupported(*target)) {
        return {PtzControlStatus::Unsupported};
    }
    const auto tour = deviceRepository.findChannelTour(channelId, tourId);
    if (!tour) {
        return {PtzControlStatus::NotFound};
    }
    if (trim(tour->tourToken).empty()) {
        return {PtzControlStatus::InternalError};
    }

    auto commandLock = tryLockPtzCommand(*target, "tour.delete");
    if (!commandLock) {
        return {PtzControlStatus::Busy};
    }

    const auto client = ptzClientForTarget(*target);
    const auto status = toPtzStatus(client.removePresetTour(
        target->ptzXAddr,
        target->profileToken,
        tour->tourToken));
    if (status != PtzControlStatus::Success) {
        return {status};
    }

    if (!deviceRepository.deleteChannelTour(channelId, tourId)) {
        LogSync::error("[device.ptz.tour.delete] DB delete failed for tour_id=", tourId);
        return {PtzControlStatus::InternalError};
    }
    return {PtzControlStatus::Success};
}
