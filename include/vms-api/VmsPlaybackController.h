#pragma once

#include "HttpServer.h"
#include "VmsPlaybackService.h"

class VmsPlaybackController {
public:
    VmsPlaybackController(HttpServer& server, VmsPlaybackService& service);

private:
    void handleGetSegmentList();
    void handleGetTimelineList();
    void handleStreamTimeRange();
    void handleGetSegment();
    void handleHlsSegment();

    HttpServer& httpServer_;
    VmsPlaybackService& service_;
};
