#include "HidppLogic.h"

#include <cstdio>
#include <vector>

using logibattery::logic::BatteryReading;
using logibattery::logic::DecodeBattery1000;
using logibattery::logic::DecodeUnifiedBatteryCapabilities;
using logibattery::logic::DecodeUnifiedBatteryStatus;
using logibattery::logic::ErrorForRequest;
using logibattery::logic::HidppErrorType;
using logibattery::logic::IsBusy;
using logibattery::logic::IsResponseForRequest;
using logibattery::logic::SelectBestBatteryReading;
using logibattery::logic::VoltageToPercent;
using mousebattery::PowerStatus;

#define CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "check failed: %s\\n", #condition); return 1; } } while (false)

int main()
{
    const std::vector<uint8_t> request = { 0x11, 1, 0x0D, 0x2A, 0, 0, 0 };
    CHECK(IsResponseForRequest(request, request));
    auto mismatch = request;
    mismatch[1] = 2; CHECK(!IsResponseForRequest(request, mismatch));
    mismatch = request; mismatch[2] = 0x0E; CHECK(!IsResponseForRequest(request, mismatch));
    mismatch = request; mismatch[3] ^= 0x10; CHECK(!IsResponseForRequest(request, mismatch));
    mismatch = request; mismatch[3] ^= 0x01; CHECK(!IsResponseForRequest(request, mismatch));

    const auto e10 = ErrorForRequest(request, { 0x10, 1, 0x8F, 0x0D, 0x2A, 0x07 });
    const auto e20 = ErrorForRequest(request, { 0x11, 1, 0xFF, 0x0D, 0x2A, 0x08 });
    CHECK(e10 && e10->type == HidppErrorType::Hidpp10 && IsBusy(*e10));
    CHECK(e20 && e20->type == HidppErrorType::Hidpp20 && IsBusy(*e20));
    const auto unsupported = ErrorForRequest(request, { 0x11, 1, 0xFF, 0x0D, 0x2A, 0x09 });
    CHECK(unsupported && !IsBusy(*unsupported));
    CHECK(!ErrorForRequest(request, { 0x11, 2, 0xFF, 0x0D, 0x2A, 0x08 }));

    BatteryReading discharging = DecodeBattery1000(75, 0);
    CHECK(discharging.percent == 75 && discharging.status == PowerStatus::Discharging);
    BatteryReading chargingUnknown = DecodeBattery1000(0, 1);
    CHECK(!chargingUnknown.percent && chargingUnknown.status == PowerStatus::Charging);
    BatteryReading full = DecodeBattery1000(0, 3);
    CHECK(full.percent == 100 && full.status == PowerStatus::Full);
    CHECK(DecodeBattery1000(50, 0).status == PowerStatus::Discharging);
    CHECK(DecodeBattery1000(50, 1).status == PowerStatus::Charging);
    CHECK(DecodeBattery1000(50, 2).status == PowerStatus::Charging);
    CHECK(DecodeBattery1000(50, 4).status == PowerStatus::Charging);
    CHECK(DecodeBattery1000(50, 5).status == PowerStatus::Unknown);
    CHECK(DecodeBattery1000(50, 6).status == PowerStatus::Unknown);
    CHECK(DecodeBattery1000(50, 7).status == PowerStatus::Unknown);
    CHECK(DecodeBattery1000(50, 5).status != PowerStatus::NotCharging);
    CHECK(DecodeBattery1000(50, 6).status != PowerStatus::NotCharging);
    CHECK(DecodeBattery1000(50, 7).status != PowerStatus::NotCharging);

    const auto socCaps = DecodeUnifiedBatteryCapabilities(0, 0x02);
    const auto levelCaps = DecodeUnifiedBatteryCapabilities(0x0F, 0x00);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 76, 1).percent == 76);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 0).status == PowerStatus::Discharging);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 1).status == PowerStatus::Charging);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 2).status == PowerStatus::Charging);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 3).status == PowerStatus::Full);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 4).status == PowerStatus::Unknown);
    CHECK(DecodeUnifiedBatteryStatus(socCaps, 50, 4).status != PowerStatus::NotCharging);
    CHECK(!DecodeUnifiedBatteryStatus(levelCaps, 0, 1).percent);
    BatteryReading voltage{ 47, PowerStatus::Discharging, false };
    CHECK(SelectBestBatteryReading(std::nullopt, chargingUnknown, voltage)->percent == 47);
    CHECK(SelectBestBatteryReading(DecodeUnifiedBatteryStatus(socCaps, 76, 1),
        chargingUnknown, voltage)->percent == 76);
    CHECK(!SelectBestBatteryReading(std::nullopt, chargingUnknown, std::nullopt)->percent);

    CHECK(VoltageToPercent(4300) == 100);
    CHECK(VoltageToPercent(4186) == 100);
    CHECK(VoltageToPercent(3800) == 47);
    CHECK(VoltageToPercent(3000) == 0);
    return 0;
}
