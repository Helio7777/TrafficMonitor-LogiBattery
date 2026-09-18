#pragma once

#include "MouseBatteryTypes.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace logibattery::logic
{
    enum class HidppErrorType { Hidpp10, Hidpp20 };

    struct HidppError
    {
        HidppErrorType type;
        uint8_t code;
    };

    struct BatteryReading
    {
        std::optional<int> percent;
        mousebattery::PowerStatus status = mousebattery::PowerStatus::Unknown;
        bool exactPercent = false;
    };

    struct UnifiedBatteryCapabilities
    {
        bool valid = false;
        bool stateOfCharge = false;
        uint8_t reportedLevels = 0;
    };

    inline std::optional<BatteryReading> SelectBestBatteryReading(
        const std::optional<BatteryReading>& unified,
        const std::optional<BatteryReading>& levelStatus,
        const std::optional<BatteryReading>& voltage)
    {
        if (unified && unified->percent)
            return unified;
        if (levelStatus && levelStatus->percent)
            return levelStatus;
        if (voltage && voltage->percent)
            return voltage;
        if (unified)
            return unified;
        if (levelStatus)
            return levelStatus;
        return voltage;
    }

    inline bool IsResponseForRequest(const std::vector<uint8_t>& request,
                                     const std::vector<uint8_t>& response)
    {
        return request.size() >= 4 && response.size() >= 4 &&
            response[1] == request[1] && response[2] == request[2] &&
            (response[3] & 0x0F) == (request[3] & 0x0F) &&
            (response[3] >> 4) == (request[3] >> 4);
    }

    inline std::optional<HidppError> ErrorForRequest(const std::vector<uint8_t>& request,
                                                     const std::vector<uint8_t>& response)
    {
        if (request.size() < 4 || response.size() < 6 || response[1] != request[1] ||
            response[3] != request[2] || response[4] != request[3])
            return std::nullopt;
        if (response[2] == 0x8F)
            return HidppError{ HidppErrorType::Hidpp10, response[5] };
        if (response[2] == 0xFF)
            return HidppError{ HidppErrorType::Hidpp20, response[5] };
        return std::nullopt;
    }

    inline bool IsBusy(const HidppError& error)
    {
        return (error.type == HidppErrorType::Hidpp10 && error.code == 0x07) ||
            (error.type == HidppErrorType::Hidpp20 && error.code == 0x08);
    }

    inline mousebattery::PowerStatus DecodeBattery1000Status(uint8_t value)
    {
        switch (value)
        {
        case 0: return mousebattery::PowerStatus::Discharging;
        case 1:
        case 2:
        case 4: return mousebattery::PowerStatus::Charging;
        case 3: return mousebattery::PowerStatus::Full;
        case 5:
        case 6:
        case 7: return mousebattery::PowerStatus::Unknown;
        default: return mousebattery::PowerStatus::Unknown;
        }
    }

    inline BatteryReading DecodeBattery1000(uint8_t capacity, uint8_t status)
    {
        BatteryReading reading;
        reading.status = DecodeBattery1000Status(status);
        if (reading.status == mousebattery::PowerStatus::Full)
        {
            reading.percent = 100;
            reading.exactPercent = true;
        }
        else if (capacity >= 1 && capacity <= 100)
        {
            // HID++ 0x1000 capacity is usable while present; zero for a
            // charging device denotes unknown, not an empty battery.
            reading.percent = capacity;
            reading.exactPercent = true;
        }
        return reading;
    }

    inline UnifiedBatteryCapabilities DecodeUnifiedBatteryCapabilities(uint8_t levels, uint8_t flags)
    {
        return UnifiedBatteryCapabilities{ true, (flags & 0x02) != 0, levels };
    }

    inline mousebattery::PowerStatus DecodeUnifiedBatteryStatus(uint8_t value)
    {
        switch (value)
        {
        case 0: return mousebattery::PowerStatus::Discharging;
        case 1:
        case 2: return mousebattery::PowerStatus::Charging;
        case 3: return mousebattery::PowerStatus::Full;
        case 4: return mousebattery::PowerStatus::Unknown;
        default: return mousebattery::PowerStatus::Unknown;
        }
    }

    inline BatteryReading DecodeUnifiedBatteryStatus(const UnifiedBatteryCapabilities& capabilities,
                                                     uint8_t stateOfCharge,
                                                     uint8_t chargingStatus)
    {
        BatteryReading reading;
        reading.status = DecodeUnifiedBatteryStatus(chargingStatus);
        if (capabilities.stateOfCharge && stateOfCharge <= 100)
        {
            reading.percent = stateOfCharge;
            reading.exactPercent = true;
        }
        else if (reading.status == mousebattery::PowerStatus::Full)
        {
            reading.percent = 100;
        }
        return reading;
    }

    int VoltageToPercent(int milliVolts);
}
