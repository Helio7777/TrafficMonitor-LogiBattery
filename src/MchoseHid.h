#pragma once

#include "MouseBatteryTypes.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <windows.h>

namespace mousebattery
{
    class MchoseBatteryService
    {
    public:
        MchoseBatteryService();
        ~MchoseBatteryService();

        MchoseBatteryService(const MchoseBatteryService&) = delete;
        MchoseBatteryService& operator=(const MchoseBatteryService&) = delete;

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
        HANDLE stopEvent_ = nullptr;

        // Match dsh-mchose-battery's 5-second refresh behavior.
        int pollSeconds_ = 5;
        int retrySeconds_ = 5;
    };
}
