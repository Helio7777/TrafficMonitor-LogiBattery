#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <devpkey.h>
#include <devpropdef.h>
#include <objbase.h>

#include "LogitechHidpp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "ole32.lib")

namespace logibattery
{
    namespace
    {
        constexpr USHORT kLogitechVid = 0x046D;
        constexpr BYTE kSoftwareId = 0x0A;
        constexpr BYTE kShortReportId = 0x10;
        constexpr BYTE kMouseDeviceType = 3;

        struct HandleCloser
        {
            void operator()(void* p) const noexcept
            {
                HANDLE h = static_cast<HANDLE>(p);
                if (h && h != INVALID_HANDLE_VALUE)
                    CloseHandle(h);
            }
        };

        using UniqueHandle = std::unique_ptr<void, HandleCloser>;

        UniqueHandle MakeHandle(HANDLE h)
        {
            if (h == INVALID_HANDLE_VALUE)
                return UniqueHandle(nullptr);
            return UniqueHandle(h);
        }

        std::wstring GuidToString(const GUID& guid)
        {
            wchar_t buffer[64]{};
            if (StringFromGUID2(guid, buffer, 64) <= 0)
                return L"";
            return buffer;
        }

        std::wstring Utf8ToWide(const std::string& input)
        {
            if (input.empty())
                return {};
            int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                             static_cast<int>(input.size()), nullptr, 0);
            if (needed <= 0)
                needed = MultiByteToWideChar(CP_UTF8, 0, input.data(),
                                             static_cast<int>(input.size()), nullptr, 0);
            if (needed <= 0)
                return L"Logitech Mouse";

            std::wstring out(static_cast<size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                out.data(), needed);
            return out;
        }

        struct EndpointInfo
        {
            std::wstring path;
            USHORT inputReportLength = 0;
            USHORT outputReportLength = 0;
        };

        struct EndpointGroup
        {
            EndpointInfo shortEndpoint;
            EndpointInfo longEndpoint;
            bool hasShort = false;
            bool hasLong = false;
        };

        std::map<std::wstring, EndpointGroup> EnumerateLogitechHidppGroups()
        {
            std::map<std::wstring, EndpointGroup> groups;

            GUID hidGuid{};
            HidD_GetHidGuid(&hidGuid);
            HDEVINFO devInfoSet = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (devInfoSet == INVALID_HANDLE_VALUE)
                return groups;

            for (DWORD index = 0;; ++index)
            {
                SP_DEVICE_INTERFACE_DATA interfaceData{};
                interfaceData.cbSize = sizeof(interfaceData);
                if (!SetupDiEnumDeviceInterfaces(devInfoSet, nullptr, &hidGuid, index, &interfaceData))
                {
                    if (GetLastError() == ERROR_NO_MORE_ITEMS)
                        break;
                    continue;
                }

                DWORD requiredSize = 0;
                SetupDiGetDeviceInterfaceDetailW(devInfoSet, &interfaceData, nullptr, 0,
                                                  &requiredSize, nullptr);
                if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                    continue;

                std::vector<BYTE> detailStorage(requiredSize);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailStorage.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                SP_DEVINFO_DATA devInfo{};
                devInfo.cbSize = sizeof(devInfo);
                if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &interfaceData, detail, requiredSize,
                                                       nullptr, &devInfo))
                    continue;

                UniqueHandle device = MakeHandle(CreateFileW(
                    detail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    nullptr));
                if (!device)
                    continue;

                HIDD_ATTRIBUTES attributes{};
                attributes.Size = sizeof(attributes);
                if (!HidD_GetAttributes(static_cast<HANDLE>(device.get()), &attributes) ||
                    attributes.VendorID != kLogitechVid)
                    continue;

                PHIDP_PREPARSED_DATA preparsed = nullptr;
                if (!HidD_GetPreparsedData(static_cast<HANDLE>(device.get()), &preparsed))
                    continue;

                HIDP_CAPS caps{};
                NTSTATUS capsStatus = HidP_GetCaps(preparsed, &caps);
                HidD_FreePreparsedData(preparsed);
                if (capsStatus != HIDP_STATUS_SUCCESS)
                    continue;

                // LGSTrayBattery classifies HID++ endpoints as vendor-defined UsagePage
                // (0xFFxx), Usage 1 = short report and Usage 2 = long report.
                if ((caps.UsagePage & 0xFF00) != 0xFF00 || (caps.Usage != 0x0001 && caps.Usage != 0x0002))
                    continue;

                GUID containerId{};
                DEVPROPTYPE propType = 0;
                DWORD propSize = 0;
                if (!SetupDiGetDevicePropertyW(devInfoSet, &devInfo, &DEVPKEY_Device_ContainerId,
                                                &propType,
                                                reinterpret_cast<PBYTE>(&containerId),
                                                sizeof(containerId), &propSize, 0) ||
                    propType != DEVPROP_TYPE_GUID)
                    continue;

                EndpointInfo endpoint;
                endpoint.path = detail->DevicePath;
                endpoint.inputReportLength = caps.InputReportByteLength;
                endpoint.outputReportLength = caps.OutputReportByteLength;

                auto& group = groups[GuidToString(containerId)];
                if (caps.Usage == 0x0001)
                {
                    group.shortEndpoint = std::move(endpoint);
                    group.hasShort = true;
                }
                else
                {
                    group.longEndpoint = std::move(endpoint);
                    group.hasLong = true;
                }
            }

            SetupDiDestroyDeviceInfoList(devInfoSet);
            return groups;
        }

        class HidEndpoint
        {
        public:
            explicit HidEndpoint(const EndpointInfo& info)
                : info_(info),
                  handle_(MakeHandle(CreateFileW(info.path.c_str(),
                                                GENERIC_READ | GENERIC_WRITE,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                nullptr, OPEN_EXISTING,
                                                FILE_FLAG_OVERLAPPED, nullptr)))
            {
            }

            bool Valid() const { return static_cast<bool>(handle_); }
            HANDLE Handle() const { return static_cast<HANDLE>(handle_.get()); }

            bool Write(const std::vector<uint8_t>& report, DWORD timeoutMs)
            {
                if (!Valid())
                    return false;

                const size_t writeLength = std::max<size_t>(info_.outputReportLength, report.size());
                std::vector<uint8_t> buffer(writeLength, 0);
                std::copy(report.begin(), report.end(), buffer.begin());

                UniqueHandle event = MakeHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!event)
                    return false;

                OVERLAPPED ov{};
                ov.hEvent = static_cast<HANDLE>(event.get());
                DWORD written = 0;
                BOOL ok = WriteFile(Handle(), buffer.data(), static_cast<DWORD>(buffer.size()),
                                    &written, &ov);
                if (!ok)
                {
                    DWORD error = GetLastError();
                    if (error != ERROR_IO_PENDING)
                        return false;
                    DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
                    if (wait != WAIT_OBJECT_0)
                    {
                        CancelIoEx(Handle(), &ov);
                        WaitForSingleObject(ov.hEvent, 50);
                        return false;
                    }
                    if (!GetOverlappedResult(Handle(), &ov, &written, FALSE))
                        return false;
                }
                return written > 0;
            }

            bool Read(std::vector<uint8_t>& report, DWORD timeoutMs)
            {
                if (!Valid())
                    return false;

                const size_t readLength = info_.inputReportLength >= 7 ? info_.inputReportLength : 20;
                std::vector<uint8_t> buffer(readLength, 0);

                UniqueHandle event = MakeHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!event)
                    return false;

                OVERLAPPED ov{};
                ov.hEvent = static_cast<HANDLE>(event.get());
                DWORD read = 0;
                BOOL ok = ReadFile(Handle(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, &ov);
                if (!ok)
                {
                    DWORD error = GetLastError();
                    if (error != ERROR_IO_PENDING)
                        return false;
                    DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
                    if (wait != WAIT_OBJECT_0)
                    {
                        CancelIoEx(Handle(), &ov);
                        WaitForSingleObject(ov.hEvent, 50);
                        return false;
                    }
                    if (!GetOverlappedResult(Handle(), &ov, &read, FALSE))
                        return false;
                }

                if (read < 7)
                    return false;
                buffer.resize(read);
                report = std::move(buffer);
                return true;
            }

            void CancelAll()
            {
                if (Valid())
                    CancelIoEx(Handle(), nullptr);
            }

        private:
            EndpointInfo info_;
            UniqueHandle handle_;
        };

        class HidppTransport
        {
        public:
            explicit HidppTransport(const EndpointGroup& group)
                : short_(group.shortEndpoint), long_(group.longEndpoint)
            {
                if (short_.Valid())
                    shortReader_ = std::thread([this] { ReaderLoop(short_); });
                if (long_.Valid())
                    longReader_ = std::thread([this] { ReaderLoop(long_); });
            }

            ~HidppTransport()
            {
                stop_.store(true);
                short_.CancelAll();
                long_.CancelAll();
                cv_.notify_all();
                if (shortReader_.joinable()) shortReader_.join();
                if (longReader_.joinable()) longReader_.join();
            }

            bool Valid() const { return short_.Valid() && long_.Valid(); }

            std::optional<std::vector<uint8_t>> Transact(const std::vector<uint8_t>& request,
                                                         DWORD timeoutMs = 250)
            {
                if (!Valid() || request.size() < 7)
                    return std::nullopt;

                std::lock_guard txLock(transactionMutex_);
                {
                    std::lock_guard queueLock(queueMutex_);
                    messages_.clear();
                }

                if (!short_.Write(request, timeoutMs))
                    return std::nullopt;

                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(timeoutMs);

                for (;;)
                {
                    std::unique_lock queueLock(queueMutex_);
                    cv_.wait_until(queueLock, deadline, [this] {
                        return stop_.load() || !messages_.empty();
                    });

                    while (!messages_.empty())
                    {
                        auto msg = std::move(messages_.front());
                        messages_.pop_front();
                        queueLock.unlock();

                        if (IsMatchingResponse(msg, request))
                            return msg;
                        if (IsErrorForDevice(msg, request[1]))
                            return std::nullopt;

                        queueLock.lock();
                    }

                    if (stop_.load() || std::chrono::steady_clock::now() >= deadline)
                        return std::nullopt;
                }
            }

        private:
            static bool IsMatchingResponse(const std::vector<uint8_t>& msg,
                                           const std::vector<uint8_t>& request)
            {
                if (msg.size() < 7 || request.size() < 7)
                    return false;
                if (msg[1] != request[1])
                    return false;
                if (msg[2] != request[2])
                    return false;
                return (msg[3] & 0x0F) == kSoftwareId;
            }

            static bool IsErrorForDevice(const std::vector<uint8_t>& msg, BYTE deviceIndex)
            {
                return msg.size() >= 7 && msg[1] == deviceIndex && msg[2] == 0x8F;
            }

            void ReaderLoop(HidEndpoint& endpoint)
            {
                while (!stop_.load())
                {
                    std::vector<uint8_t> msg;
                    if (!endpoint.Read(msg, 500))
                        continue;
                    {
                        std::lock_guard lock(queueMutex_);
                        if (messages_.size() >= 32)
                            messages_.pop_front();
                        messages_.push_back(std::move(msg));
                    }
                    cv_.notify_all();
                }
            }

            HidEndpoint short_;
            HidEndpoint long_;
            std::thread shortReader_;
            std::thread longReader_;
            std::atomic<bool> stop_{ false };
            std::mutex transactionMutex_;
            std::mutex queueMutex_;
            std::condition_variable cv_;
            std::deque<std::vector<uint8_t>> messages_;
        };

        std::vector<uint8_t> MakeRequest(BYTE deviceIndex, BYTE featureIndex,
                                         BYTE functionId, BYTE p0 = 0,
                                         BYTE p1 = 0, BYTE p2 = 0)
        {
            return { kShortReportId, deviceIndex, featureIndex,
                     static_cast<BYTE>((functionId << 4) | kSoftwareId),
                     p0, p1, p2 };
        }

        bool Ping(HidppTransport& transport, BYTE deviceIndex)
        {
            static std::atomic<unsigned> payloadCounter{ 0x55 };
            BYTE payload = static_cast<BYTE>(++payloadCounter);
            auto ret = transport.Transact(MakeRequest(deviceIndex, 0x00, 0x01, 0x00, 0x00, payload), 180);
            return ret && ret->size() >= 7 && (*ret)[6] == payload;
        }

        std::optional<BYTE> GetFeatureIndex(HidppTransport& transport, BYTE deviceIndex,
                                            USHORT featureId)
        {
            auto ret = transport.Transact(MakeRequest(deviceIndex, 0x00, 0x00,
                                                      static_cast<BYTE>(featureId >> 8),
                                                      static_cast<BYTE>(featureId & 0xFF), 0), 250);
            if (!ret || ret->size() < 7)
                return std::nullopt;
            BYTE index = (*ret)[4];
            if (index == 0 && featureId != 0x0000)
                return std::nullopt;
            return index;
        }

        struct HidppMouse
        {
            BYTE deviceIndex = 0;
            std::wstring name;
            std::optional<BYTE> battery1000;
            std::optional<BYTE> battery1001;
            std::optional<BYTE> battery1004;
        };

        std::optional<HidppMouse> ProbeMouse(HidppTransport& transport, BYTE deviceIndex)
        {
            if (!Ping(transport, deviceIndex))
                return std::nullopt;

            auto nameFeature = GetFeatureIndex(transport, deviceIndex, 0x0005);
            if (!nameFeature)
                return std::nullopt;

            auto typeRet = transport.Transact(MakeRequest(deviceIndex, *nameFeature, 0x02), 250);
            if (!typeRet || typeRet->size() < 5 || (*typeRet)[4] != kMouseDeviceType)
                return std::nullopt;

            HidppMouse mouse;
            mouse.deviceIndex = deviceIndex;

            auto lengthRet = transport.Transact(MakeRequest(deviceIndex, *nameFeature, 0x00), 250);
            if (lengthRet && lengthRet->size() >= 5)
            {
                const size_t expectedLength = (*lengthRet)[4];
                std::string utf8Name;
                utf8Name.reserve(expectedLength);
                size_t offset = 0;
                while (offset < expectedLength && offset < 255)
                {
                    auto chunk = transport.Transact(MakeRequest(deviceIndex, *nameFeature, 0x01,
                                                                static_cast<BYTE>(offset)), 250);
                    if (!chunk || chunk->size() <= 4)
                        break;
                    for (size_t i = 4; i < chunk->size() && utf8Name.size() < expectedLength; ++i)
                    {
                        if ((*chunk)[i] == 0)
                            break;
                        utf8Name.push_back(static_cast<char>((*chunk)[i]));
                    }
                    if (utf8Name.size() <= offset)
                        break;
                    offset = utf8Name.size();
                }
                mouse.name = Utf8ToWide(utf8Name);
            }
            if (mouse.name.empty())
                mouse.name = L"Logitech Mouse";

            mouse.battery1000 = GetFeatureIndex(transport, deviceIndex, 0x1000);
            mouse.battery1001 = GetFeatureIndex(transport, deviceIndex, 0x1001);
            mouse.battery1004 = GetFeatureIndex(transport, deviceIndex, 0x1004);

            if (!mouse.battery1000 && !mouse.battery1001 && !mouse.battery1004)
                return std::nullopt;
            return mouse;
        }

        PowerStatus DecodeLevelStatus(BYTE value, bool feature1000)
        {
            // Match LGSTrayBattery's status mapping for 0x1000/0x1004.
            switch (value)
            {
            case 0: return PowerStatus::Discharging;
            case 1:
            case 2: return PowerStatus::Charging;
            case 3: return PowerStatus::Full;
            case 4: return feature1000 ? PowerStatus::Charging : PowerStatus::NotCharging;
            default: return PowerStatus::NotCharging;
            }
        }

        int VoltageToPercent(int mv)
        {
            // Same generic 3.7 V Li-Po curve used by LGSTrayBattery's native HID path.
            // Kept here because HID++ feature 0x1001 reports voltage rather than percent.
            static constexpr std::array<int, 100> kMilliVoltLut = {
                4186, 4156, 4143, 4133, 4122, 4113, 4103, 4094, 4086, 4075,
                4067, 4059, 4051, 4043, 4035, 4027, 4019, 4011, 4003, 3997,
                3989, 3983, 3976, 3969, 3961, 3955, 3949, 3942, 3935, 3929,
                3922, 3916, 3909, 3902, 3896, 3890, 3883, 3877, 3870, 3865,
                3859, 3853, 3848, 3842, 3837, 3833, 3828, 3824, 3819, 3815,
                3811, 3808, 3804, 3800, 3797, 3793, 3790, 3787, 3784, 3781,
                3778, 3775, 3772, 3770, 3767, 3764, 3762, 3759, 3757, 3754,
                3751, 3748, 3744, 3741, 3737, 3734, 3730, 3726, 3724, 3720,
                3717, 3714, 3710, 3706, 3702, 3697, 3693, 3688, 3683, 3677,
                3671, 3666, 3662, 3658, 3654, 3646, 3633, 3612, 3579, 3537,
            };

            for (size_t i = 0; i < kMilliVoltLut.size(); ++i)
            {
                if (mv > kMilliVoltLut[i])
                    return static_cast<int>(kMilliVoltLut.size() - i);
            }
            return 0;
        }

        std::optional<BatterySnapshot> ReadBattery(HidppTransport& transport,
                                                   const HidppMouse& mouse)
        {
            BatterySnapshot snapshot;
            snapshot.online = true;
            snapshot.deviceName = mouse.name;

            if (mouse.battery1000)
            {
                auto ret = transport.Transact(MakeRequest(mouse.deviceIndex, *mouse.battery1000, 0x00), 300);
                if (ret && ret->size() >= 7)
                {
                    snapshot.percent = std::clamp<int>((*ret)[4], 0, 100);
                    snapshot.status = DecodeLevelStatus((*ret)[6], true);
                    snapshot.source = L"0x1000";
                    return snapshot;
                }
            }

            if (mouse.battery1001)
            {
                auto ret = transport.Transact(MakeRequest(mouse.deviceIndex, *mouse.battery1001, 0x00), 300);
                if (ret && ret->size() >= 7)
                {
                    snapshot.milliVolts = (static_cast<int>((*ret)[4]) << 8) | (*ret)[5];
                    snapshot.percent = VoltageToPercent(snapshot.milliVolts);
                    BYTE flags = (*ret)[6];
                    if ((flags & 0x80) == 0)
                    {
                        snapshot.status = PowerStatus::Discharging;
                    }
                    else
                    {
                        switch (flags & 0x07)
                        {
                        case 0: snapshot.status = PowerStatus::Charging; break;
                        case 1: snapshot.status = PowerStatus::Full; break;
                        case 2: snapshot.status = PowerStatus::NotCharging; break;
                        default: snapshot.status = PowerStatus::Unknown; break;
                        }
                    }
                    snapshot.source = L"0x1001";
                    return snapshot;
                }
            }

            if (mouse.battery1004)
            {
                auto ret = transport.Transact(MakeRequest(mouse.deviceIndex, *mouse.battery1004, 0x01), 300);
                if (ret && ret->size() >= 7)
                {
                    snapshot.percent = std::clamp<int>((*ret)[4], 0, 100);
                    snapshot.status = DecodeLevelStatus((*ret)[6], false);
                    snapshot.source = L"0x1004";
                    return snapshot;
                }
            }

            return std::nullopt;
        }
    }

    LogitechBatteryService::LogitechBatteryService() = default;

    LogitechBatteryService::~LogitechBatteryService()
    {
        Stop();
    }

    void LogitechBatteryService::Start()
    {
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true))
            return;

        {
            std::lock_guard lock(wakeMutex_);
            stopRequested_ = false;
            refreshRequested_ = true;
        }
        worker_ = std::thread([this] { WorkerMain(); });
    }

    void LogitechBatteryService::Stop()
    {
        if (!started_.load())
            return;

        {
            std::lock_guard lock(wakeMutex_);
            stopRequested_ = true;
            refreshRequested_ = true;
        }
        wakeCv_.notify_all();
        if (worker_.joinable())
            worker_.join();
        started_.store(false);
    }

    void LogitechBatteryService::RequestRefresh()
    {
        Start();
        {
            std::lock_guard lock(wakeMutex_);
            refreshRequested_ = true;
        }
        wakeCv_.notify_all();
    }

    BatterySnapshot LogitechBatteryService::GetSnapshot() const
    {
        std::lock_guard lock(snapshotMutex_);
        return snapshot_;
    }

    void LogitechBatteryService::WorkerMain()
    {
        for (;;)
        {
            {
                std::lock_guard lock(wakeMutex_);
                if (stopRequested_)
                    break;
                refreshRequested_ = false;
            }

            BatterySnapshot next = QueryOnce();
            {
                std::lock_guard lock(snapshotMutex_);
                snapshot_ = std::move(next);
            }

            const bool isOnline = GetSnapshot().online;
            const int waitSeconds = isOnline ? pollSeconds_ : retrySeconds_;

            std::unique_lock lock(wakeMutex_);
            wakeCv_.wait_for(lock, std::chrono::seconds(waitSeconds), [this] {
                return stopRequested_ || refreshRequested_;
            });
            if (stopRequested_)
                break;
        }
    }

    BatterySnapshot LogitechBatteryService::QueryOnce()
    {
        BatterySnapshot result;
        const auto groups = EnumerateLogitechHidppGroups();
        if (groups.empty())
        {
            result.error = L"没有找到 Logitech HID++ 短/长报文接口。";
            return result;
        }

        bool foundCompleteGroup = false;
        bool foundMouse = false;

        for (const auto& [_, group] : groups)
        {
            if (!group.hasShort || !group.hasLong)
                continue;
            foundCompleteGroup = true;

            HidppTransport transport(group);
            if (!transport.Valid())
                continue;

            // Logitech receivers conventionally expose paired devices at 1..6.
            // 0xFF is also tried last for direct/wired HID++ devices.
            constexpr std::array<BYTE, 7> kCandidateIndices = { 1, 2, 3, 4, 5, 6, 0xFF };
            for (BYTE deviceIndex : kCandidateIndices)
            {
                auto mouse = ProbeMouse(transport, deviceIndex);
                if (!mouse)
                    continue;
                foundMouse = true;

                auto battery = ReadBattery(transport, *mouse);
                if (battery)
                    return *battery;
            }
        }

        if (!foundCompleteGroup)
            result.error = L"检测到 Logitech HID，但未找到可配对的 HID++ short/long 接口。";
        else if (!foundMouse)
            result.error = L"未发现 HID++ 2.0 鼠标，或鼠标正在休眠/离线。";
        else
            result.error = L"发现 Logitech 鼠标，但当前无法读取电量。";
        return result;
    }
}
