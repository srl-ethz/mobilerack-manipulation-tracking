// Copyright 2018 Yasu
#pragma once

#include <stdio.h>
#include <thread>
#include <pthread.h>

#include "RTProtocol.h"
#include "RTPacket.h"

#include "fmt/core.h"

#include "SoftTrunk_common.h"

/**
 * @brief wraps the qualisys_cpp_sdk library for easy access to motion tracking data
 * @details Frames are obtained in a separate thread. Based on RigidBodyStreaming.cpp from the qualisys_cpp_sdk repo.
 */
class QualisysClient {
public:
    /**
     * @param num_frames how many _frames you want to track. The frame labels should be "0", "1", ..., "<num_frames>".
     * Can't read 2 (or more) digit labels for now.
     */
    QualisysClient(int num_frames);

    ~QualisysClient();

    /**
     * Get a new frame from the listener.
     * @todo implement mutex lock
     * @todo get time too
     * @param frames vector of frames received from motion track. index corresponds to frame label.
     */
    void getData(std::vector<Eigen::Transform<double, 3, Eigen::Affine>> &frames);

private:
    CRTProtocol rtProtocol;
    const int majorVersion = 1;
    const int minorVersion = 19;
    const bool bigEndian = false;
    unsigned short udpPort = 6734;

    std::vector<Eigen::Transform<double, 3, Eigen::Affine>> _frames;
    std::thread motiontrack_thread;

    void motiontrack_loop();

    bool connect();
};

