#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "MouseBatteryTypes.h"

namespace logibattery
{
    using mousebattery::BatterySnapshot;
    using mousebattery::PowerStatus;

    class LogitechBatteryService
    {
    public:
        LogitechBatteryService();
        ~LogitechBatteryService();

        LogitechBatteryService(const LogitechBatteryService&) = delete;
        LogitechBatteryService& operator=(const LogitechBatteryService&) = delete;

        void Start();
        void Stop();
        void RequestRefresh();
        BatterySnapshot GetSnapshot() const;

    private:
        void WorkerMain();
        BatterySnapshot QueryOnce();

        mutable std::mutex snapshotMutex_;
        BatterySnapshot snapshot_;

        std::mutex wakeMutex_;
        std::condition_variable wakeCv_;
        std::thread worker_;
        std::atomic<bool> started_{ false };
        bool stopRequested_ = false;
        bool refreshRequested_ = false;

        // Keep the default deliberately conservative. Frequent HID++ polling can
        // keep some wireless devices awake and reduce battery life.
        int pollSeconds_ = 600;
        int retrySeconds_ = 10;
    };
}
