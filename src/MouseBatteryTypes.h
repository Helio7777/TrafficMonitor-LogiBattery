#pragma once

#include <string>

namespace mousebattery
{
    enum class PowerStatus
    {
        Unknown,
        Discharging,
        Charging,
        Full,
        NotCharging,
    };

    struct BatterySnapshot
    {
        bool online = false;
        std::wstring deviceName;
        int percent = -1;
        int milliVolts = -1;
        PowerStatus status = PowerStatus::Unknown;
        std::wstring source;
        std::wstring connectionMode;
        std::wstring error;
    };
}
