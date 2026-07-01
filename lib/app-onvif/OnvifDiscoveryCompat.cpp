#include "OnvifDiscoveryCompat.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "LogSync.h"

namespace {

constexpr const char* kMulticastAddress = "239.255.255.250";
constexpr std::uint16_t kDiscoveryPort = 3702;

class UdpSocket {
public:
    explicit UdpSocket(int descriptor = -1) : descriptor_(descriptor) {}
    ~UdpSocket() { if (descriptor_ >= 0) ::close(descriptor_); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    int get() const { return descriptor_; }
    explicit operator bool() const { return descriptor_ >= 0; }
private:
    int descriptor_;
};

std::string makeUuid() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& byte : bytes) byte = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "urn:uuid:";
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
        result.push_back(hex[bytes[i] >> 4U]);
        result.push_back(hex[bytes[i] & 0x0fU]);
    }
    return result;
}

bool localNameEquals(std::string_view qualifiedName, std::string_view localName) {
    const std::size_t colon = qualifiedName.rfind(':');
    if (colon != std::string_view::npos) qualifiedName.remove_prefix(colon + 1);
    return qualifiedName == localName;
}

std::optional<std::pair<std::size_t, std::string>> findOpeningTag(
    std::string_view xml, std::string_view localName, std::size_t offset = 0) {
    while ((offset = xml.find('<', offset)) != std::string_view::npos) {
        const std::size_t start = offset + 1;
        if (start >= xml.size()) return std::nullopt;
        if (xml[start] == '/' || xml[start] == '!' || xml[start] == '?') {
            ++offset;
            continue;
        }
        std::size_t end = start;
        while (end < xml.size() && xml[end] != '>' && xml[end] != '/'
            && !std::isspace(static_cast<unsigned char>(xml[end]))) ++end;
        const std::string_view name = xml.substr(start, end - start);
        if (localNameEquals(name, localName)) {
            return std::pair<std::size_t, std::string>{offset, std::string(name)};
        }
        ++offset;
    }
    return std::nullopt;
}

std::string decodeEntities(std::string value) {
    const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"},
    };
    for (const auto& entity : entities) {
        std::size_t position = 0;
        while ((position = value.find(entity.first, position)) != std::string::npos) {
            value.replace(position, std::strlen(entity.first), entity.second);
            position += std::strlen(entity.second);
        }
    }
    return value;
}

std::string elementText(std::string_view xml, std::string_view localName) {
    const auto opening = findOpeningTag(xml, localName);
    if (!opening) return {};
    const std::size_t contentStart = xml.find('>', opening->first);
    if (contentStart == std::string_view::npos) return {};
    const std::string closing = "</" + opening->second;
    const std::size_t contentEnd = xml.find(closing, contentStart + 1);
    if (contentEnd == std::string_view::npos) return {};
    std::string value(xml.substr(contentStart + 1, contentEnd - contentStart - 1));
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return decodeEntities(value.substr(first, last - first + 1));
}

std::vector<std::string> splitWhitespace(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string item;
    while (stream >> item) result.push_back(item);
    return result;
}

bool sameDevice(const OnvifDiscoveredDevice& lhs, const OnvifDiscoveredDevice& rhs) {
    if (!lhs.endpointReference.empty() && lhs.endpointReference == rhs.endpointReference) return true;
    for (const auto& left : lhs.xaddrs) {
        if (std::find(rhs.xaddrs.begin(), rhs.xaddrs.end(), left) != rhs.xaddrs.end()) return true;
    }
    return false;
}

void addDevice(std::vector<OnvifDiscoveredDevice>& devices, OnvifDiscoveredDevice device) {
    if (device.endpointReference.empty() && device.xaddrs.empty()) return;
    const auto found = std::find_if(devices.begin(), devices.end(),
        [&](const OnvifDiscoveredDevice& current) { return sameDevice(current, device); });
    if (found == devices.end()) devices.push_back(std::move(device));
}

OnvifDiscoveredDevice parseProbeMatch(std::string_view xml) {
    OnvifDiscoveredDevice device;
    const auto match = findOpeningTag(xml, "ProbeMatch");
    if (!match) return device;
    xml.remove_prefix(match->first);
    device.endpointReference = elementText(xml, "Address");
    device.types = elementText(xml, "Types");
    device.scopes = elementText(xml, "Scopes");
    device.xaddrs = splitWhitespace(elementText(xml, "XAddrs"));
    const std::string version = elementText(xml, "MetadataVersion");
    if (!version.empty()) {
        try { device.metadataVersion = static_cast<std::uint32_t>(std::stoul(version)); }
        catch (...) { device.metadataVersion = 0; }
    }
    device.online = true;
    return device;
}

std::string makeProbe(const std::string& id, std::string_view discovery, std::string_view addressing) {
    const std::string anonymous = addressing == "http://www.w3.org/2005/08/addressing"
        ? "http://www.w3.org/2005/08/addressing/anonymous"
        : "http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous";
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\""
        + std::string(addressing) + "\" xmlns:d=\"" + std::string(discovery)
        + "\" xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\"><s:Header>"
        "<a:MessageID>" + id + "</a:MessageID><a:ReplyTo><a:Address>" + anonymous
        + "</a:Address></a:ReplyTo><a:To s:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>"
        "<a:Action s:mustUnderstand=\"true\">" + std::string(discovery)
        + "/Probe</a:Action></s:Header><s:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types>"
        "</d:Probe></s:Body></s:Envelope>";
}

bool sendProbe(int socket, const std::string& message) {
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kDiscoveryPort);
    if (::inet_pton(AF_INET, kMulticastAddress, &target.sin_addr) != 1) return false;
    return ::sendto(socket, message.data(), message.size(), 0,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == static_cast<ssize_t>(message.size());
}

} // namespace

std::vector<OnvifDiscoveredDevice> discoverOnvifDevicesCompat(const OnvifDiscoveryOptions& options) {
    UdpSocket socket(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!socket) {
        LogSync::error("[ONVIF] discovery socket failed: ", std::strerror(errno));
        return {};
    }

    unsigned char ttl = 1;
    ::setsockopt(socket.get(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(static_cast<std::uint16_t>(std::max(0, options.listenPort)));
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        LogSync::error("[ONVIF] discovery bind failed: ", std::strerror(errno));
        return {};
    }
    socklen_t localLength = sizeof(local);
    ::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&local), &localLength);
    LogSync::info("[ONVIF][DISCOVERY] active socket UDP port ", ntohs(local.sin_port),
        ", timeout ", options.listenTimeout.count(), " ms");

    struct Profile { const char* discovery; const char* addressing; };
    const Profile profiles[] = {
        {"http://schemas.xmlsoap.org/ws/2005/04/discovery", "http://schemas.xmlsoap.org/ws/2004/08/addressing"},
        {"http://schemas.xmlsoap.org/ws/2005/04/discovery", "http://www.w3.org/2005/08/addressing"},
        {"http://docs.oasis-open.org/ws-dd/ns/discovery/2009/01", "http://www.w3.org/2005/08/addressing"},
    };
    std::vector<std::string> messageIds;
    for (const Profile& profile : profiles) {
        messageIds.push_back(makeUuid());
        LogSync::info("[ONVIF][DISCOVERY] sending Probe id=", messageIds.back(),
            ", discovery=", profile.discovery, ", addressing=", profile.addressing);
        if (!sendProbe(socket.get(), makeProbe(messageIds.back(), profile.discovery, profile.addressing))) {
            LogSync::error("[ONVIF] discovery probe send failed: ", std::strerror(errno));
        }
    }

    std::vector<OnvifDiscoveredDevice> devices;
    const auto deadline = std::chrono::steady_clock::now() + options.listenTimeout;
    std::array<char, 65536> buffer{};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        pollfd descriptor{socket.get(), POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, static_cast<int>(std::max<std::int64_t>(1, remaining)));
        if (ready == 0) break;
        if (ready < 0) {
            if (errno == EINTR) continue;
            LogSync::error("[ONVIF] discovery receive failed: ", std::strerror(errno));
            break;
        }
        sockaddr_in source{};
        socklen_t sourceLength = sizeof(source);
        const ssize_t size = ::recvfrom(socket.get(), buffer.data(), buffer.size(), 0,
            reinterpret_cast<sockaddr*>(&source), &sourceLength);
        if (size <= 0) continue;
        char sourceAddress[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &source.sin_addr, sourceAddress, sizeof(sourceAddress));
        LogSync::info("[ONVIF][DISCOVERY] received ", size, " bytes from ",
            sourceAddress, ":", ntohs(source.sin_port));
        const std::string_view xml(buffer.data(), static_cast<std::size_t>(size));
        const std::string relatesTo = elementText(xml, "RelatesTo");
        LogSync::info("[ONVIF][DISCOVERY] RelatesTo=",
            relatesTo.empty() ? "<missing>" : relatesTo);
        if (!relatesTo.empty()
            && std::find(messageIds.begin(), messageIds.end(), relatesTo) == messageIds.end()) {
            LogSync::info("[ONVIF][DISCOVERY] ignored: RelatesTo does not match this discovery run");
            continue;
        }
        OnvifDiscoveredDevice device = parseProbeMatch(xml);
        LogSync::info("[ONVIF][DISCOVERY] parsed endpoint=",
            device.endpointReference.empty() ? "<missing>" : device.endpointReference,
            ", xaddrs=", device.xaddrs.size(), ", types=",
            device.types.empty() ? "<missing>" : device.types);
        if (device.endpointReference.empty() && device.xaddrs.empty()) {
            LogSync::info("[ONVIF][DISCOVERY] ignored: ProbeMatch fields not recoverable");
            continue;
        }
        addDevice(devices, std::move(device));
    }

    LogSync::info("[ONVIF] discovery found ", devices.size(), " device(s)");
    return devices;
}
