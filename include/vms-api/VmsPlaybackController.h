#ifndef __VMSPLAYBACKCONTROLLER_H__
#define __VMSPLAYBACKCONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class VmsPlaybackController {
private:
    HttpServer& httpServer;

public:
    VmsPlaybackController(HttpServer& server);
    ~VmsPlaybackController();

    void handleGetSegmentList();
    void handleGetTimelineList();
    void handleStreamTimeRange();
    void handleDownloadTimeRange();
    void handleGetSegment();
    void handleSegmentStream();
    void handleSegmentDownload();
    void handleSegmentThumbnail();
};

#endif // __VMSPLAYBACKCONTROLLER_H__