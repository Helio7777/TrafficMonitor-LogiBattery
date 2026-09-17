#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <cassert>
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
        constexpr BYTE kLongReportId = 0x11;
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

        enum class IoWaitResult
        {
            Completed,
            Timeout,
            Stopped,
            Failed,
        };

        IoWaitResult WaitOverlappedIo(HANDLE device,
                                      OVERLAPPED& ov,
                                      DWORD timeoutMs,
                                      HANDLE stopEvent,
                                      DWORD& transferred)
        {
            HANDLE waitHandles[2] = { ov.hEvent, stopEvent };
            const DWORD waitCount = stopEvent ? 2 : 1;
            const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles, FALSE, timeoutMs);
            if (waitResult == WAIT_OBJECT_0)
            {
                if (!GetOverlappedResult(device, &ov, &transferred, FALSE))
                    return IoWaitResult::Failed;
                return IoWaitResult::Completed;
            }

            if (waitResult == WAIT_TIMEOUT || (stopEvent && waitResult == WAIT_OBJECT_0 + 1))
            {
                const bool stopped = stopEvent && waitResult == WAIT_OBJECT_0 + 1;
                CancelIoEx(device, &ov);
                DWORD completionBytes = 0;
                if (!GetOverlappedResult(device, &ov, &completionBytes, TRUE))
                {
                    const DWORD error = GetLastError();
                    if (error != ERROR_OPERATION_ABORTED)
                        return IoWaitResult::Failed;
                }
                transferred = completionBytes;
                return stopped ? IoWaitResult::Stopped : IoWaitResult::Timeout;
            }

            return IoWaitResult::Failed;
        }

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

            bool Write(const std::vector<uint8_t>& report, DWORD timeoutMs, HANDLE stopEvent)
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
                    const IoWaitResult waitResult =
                        WaitOverlappedIo(Handle(), ov, timeoutMs, stopEvent, written);
                    if (waitResult != IoWaitResult::Completed)
                        return false;
                }
                return written > 0;
            }

            bool Read(std::vector<uint8_t>& report, DWORD timeoutMs, HANDLE stopEvent)
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
                    const IoWaitResult waitResult =
                        WaitOverlappedIo(Handle(), ov, timeoutMs, stopEvent, read);
                    if (waitResult != IoWaitResult::Completed)
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

        enum class HidppErrorType
        {
            None,
            Hidpp10,
            Hidpp20,
        };

        struct HidppError
        {
            HidppErrorType type = HidppErrorType::None;
            BYTE code = 0;
        };

        class HidppTransport
        {
        public:
            HidppTransport(const EndpointGroup& group, HANDLE stopEvent)
                : short_(group.shortEndpoint), long_(group.longEndpoint), stopEvent_(stopEvent)
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

            bool Valid() const { return short_.Valid() || long_.Valid(); }
            bool HasShortReport() const { return short_.Valid(); }
            bool HasLongReport() const { return long_.Valid(); }

            std::optional<std::vector<uint8_t>> Transact(const std::vector<uint8_t>& request,
                                                         DWORD timeoutMs = 250)
            {
                if (!Valid() || request.size() < 7)
                    return std::nullopt;

                std::vector<uint8_t> txReport;
                HidEndpoint* writeEndpoint = SelectWriteEndpoint(request, txReport);
                if (!writeEndpoint)
                    return std::nullopt;

                std::lock_guard txLock(transactionMutex_);
                {
                    std::lock_guard queueLock(queueMutex_);
                    messages_.clear();
                }

                if (!writeEndpoint->Write(txReport, timeoutMs, stopEvent_))
                    return std::nullopt;

                const auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

                for (;;)
                {
                    std::vector<uint8_t> msg;
                    {
                        std::unique_lock queueLock(queueMutex_);
                        while (messages_.empty() && !stop_.load())
                        {
                            const auto now = std::chrono::steady_clock::now();
                            if (now >= deadline)
                                return std::nullopt;
                            cv_.wait_until(queueLock, deadline);
                        }

                        if (stop_.load())
                            return std::nullopt;

                        if (messages_.empty())
                            continue;
                        msg = std::move(messages_.front());
                        messages_.pop_front();
                    }

                    if (IsResponseForRequest(request, msg))
                        return msg;
                    if (IsErrorForRequest(request, msg).has_value())
                        return std::nullopt;

                    if (std::chrono::steady_clock::now() >= deadline)
                        return std::nullopt;
                }
            }

            static bool IsResponseForRequest(const std::vector<uint8_t>& request,
                                             const std::vector<uint8_t>& response)
            {
                if (request.size() < 4 || response.size() < 4)
                    return false;
                if (response[1] != request[1]) // device index
                    return false;
                if (response[2] != request[2]) // feature index
                    return false;
                if ((response[3] & 0x0F) != (request[3] & 0x0F)) // software id
                    return false;
                return (response[3] >> 4) == (request[3] >> 4); // function id
            }

            static std::optional<HidppError> IsErrorForRequest(
                const std::vector<uint8_t>& request,
                const std::vector<uint8_t>& response)
            {
                if (request.size() < 4 || response.size() < 5)
                    return std::nullopt;
                if (response[1] != request[1])
                    return std::nullopt;

                if (response[2] == 0x8F)
                {
                    if (response.size() < 6)
                        return std::nullopt;
                    if (response[3] != request[2] || response[4] != request[3])
                        return std::nullopt;
                    return HidppError{ HidppErrorType::Hidpp10, response[5] };
                }

                if (response[2] == 0xFF)
                {
                    if (response.size() < 6)
                        return std::nullopt;
                    if (response[3] != request[2] || response[4] != request[3])
                        return std::nullopt;
                    return HidppError{ HidppErrorType::Hidpp20, response[5] };
                }

                return std::nullopt;
            }

        private:
            HidEndpoint* SelectWriteEndpoint(const std::vector<uint8_t>& request,
                                             std::vector<uint8_t>& txReport)
            {
                if (request.empty())
                    return nullptr;

                if (request[0] == kLongReportId)
                {
                    if (!long_.Valid())
                        return nullptr;
                    txReport = request;
                    return &long_;
                }

                if (short_.Valid())
                {
                    txReport = request;
                    if (txReport[0] != kShortReportId)
                        txReport[0] = kShortReportId;
                    return &short_;
                }

                if (!long_.Valid())
                    return nullptr;

                txReport.assign(20, 0);
                const size_t copyBytes = std::min<size_t>(request.size(), txReport.size());
                std::copy_n(request.begin(), copyBytes, txReport.begin());
                txReport[0] = kLongReportId;
                return &long_;
            }

            void ReaderLoop(HidEndpoint& endpoint)
            {
                while (!stop_.load())
                {
                    std::vector<uint8_t> msg;
                    if (!endpoint.Read(msg, 500, stopEvent_))
                    {
                        if (stop_.load())
                            break;
                        continue;
                    }
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
            HANDLE stopEvent_ = nullptr;
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
                                         BYTE p1 = 0, BYTE p2 = 0,
                                         bool longReport = false)
        {
            if (!longReport)
            {
                return { kShortReportId, deviceIndex, featureIndex,
                    static_cast<BYTE>((functionId << 4) | kSoftwareId),
                    p0, p1, p2 };
            }
            std::vector<uint8_t> request(20, 0);
            request[0] = kLongReportId;
            request[1] = deviceIndex;
            request[2] = featureIndex;
            request[3] = static_cast<BYTE>((functionId << 4) | kSoftwareId);
            request[4] = p0;
            request[5] = p1;
            request[6] = p2;
            return request;
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
                if (mv >= kMilliVoltLut[i])
                    return std::clamp(static_cast<int>(kMilliVoltLut.size() - i), 0, 100);
            }
            return 0;
        }

#ifndef NDEBUG
        void RunHidppLogicSelfTest()
        {
            const auto request = MakeRequest(0x01, 0x0D, 0x02, 0xAA, 0xBB, 0xCC);
            auto response = request;
            assert(HidppTransport::IsResponseForRequest(request, response));

            response[3] = static_cast<BYTE>(((0x03) << 4) | (response[3] & 0x0F));
            assert(!HidppTransport::IsResponseForRequest(request, response));
            response = request;
            response[2] = 0x0E;
            assert(!HidppTransport::IsResponseForRequest(request, response));
            response = request;
            response[1] = 0x02;
            assert(!HidppTransport::IsResponseForRequest(request, response));
            response = request;
            response[3] = static_cast<BYTE>((response[3] & 0xF0) | 0x03);
            assert(!HidppTransport::IsResponseForRequest(request, response));

            const std::vector<uint8_t> hidpp10Error = { kShortReportId, 0x01, 0x8F, 0x0D, request[3], 0x02, 0x00 };
            const auto parsed10 = HidppTransport::IsErrorForRequest(request, hidpp10Error);
            assert(parsed10 && parsed10->type == HidppErrorType::Hidpp10);
            const std::vector<uint8_t> hidpp20Error = { kShortReportId, 0x01, 0xFF, 0x0D, request[3], 0x09, 0x00 };
            const auto parsed20 = HidppTransport::IsErrorForRequest(request, hidpp20Error);
            assert(parsed20 && parsed20->type == HidppErrorType::Hidpp20);
            assert(!HidppTransport::IsErrorForRequest(request, request));
            const std::vector<uint8_t> otherDeviceError = { kShortReportId, 0x02, 0xFF, 0x0D, request[3], 0x09, 0x00 };
            assert(!HidppTransport::IsErrorForRequest(request, otherDeviceError));

            assert(VoltageToPercent(4300) == 100);
            assert(VoltageToPercent(4186) == 100);
            assert(VoltageToPercent(3800) == 47);
            assert(VoltageToPercent(3000) == 0);
        }
#endif

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

    LogitechBatteryService::LogitechBatteryService()
    {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#ifndef NDEBUG
        static const bool kSelfTestRan = [] {
            RunHidppLogicSelfTest();
            return true;
        }();
        (void)kSelfTestRan;
#endif
    }

    LogitechBatteryService::~LogitechBatteryService()
    {
        Stop();
        if (stopEvent_)
        {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
    }

    void LogitechBatteryService::Start()
    {
        bool expected = false;
        if (!started_.compare_exchange_strong(expected, true))
            return;

        if (stopEvent_)
            ResetEvent(stopEvent_);

        {
            std::lock_guard lock(wakeMutex_);
            stopRequested_ = false;
            refreshRequested_ = true;
        }
        worker_ = std::thread([this] { WorkerMain(); });
    }

    void LogitechBatteryService::Stop()
    {
        if (!started_.exchange(false))
            return;

        {
            std::lock_guard lock(wakeMutex_);
            stopRequested_ = true;
            refreshRequested_ = true;
        }
        if (stopEvent_)
            SetEvent(stopEvent_);
        wakeCv_.notify_all();
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
            worker_.join();
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
        if (stopEvent_ && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0)
            return result;

        const auto groups = EnumerateLogitechHidppGroups();
        if (groups.empty())
        {
            result.error = L"没有找到 Logitech HID++ 短/长报文接口。";
            return result;
        }

        bool foundEndpointGroup = false;
        bool foundMouse = false;
        bool foundShortOnly = false;
        bool foundLongOnly = false;

        for (const auto& [_, group] : groups)
        {
            if (stopEvent_ && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0)
                return result;

            if (!group.hasShort && !group.hasLong)
                continue;
            foundEndpointGroup = true;
            foundShortOnly = foundShortOnly || (group.hasShort && !group.hasLong);
            foundLongOnly = foundLongOnly || (!group.hasShort && group.hasLong);

            HidppTransport transport(group, stopEvent_);
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

        if (!foundEndpointGroup)
            result.error = L"检测到 Logitech HID，但没有可用的 HID++ endpoint。";
        else if (!foundMouse)
            result.error = L"未发现 HID++ 2.0 鼠标，或鼠标正在休眠/离线。";
        else
            result.error = L"发现 Logitech 鼠标，但当前无法读取电量。";

        if (!result.error.empty() && (foundShortOnly || foundLongOnly))
        {
            result.error += L"（已按设备能力尝试 short-only/long-only endpoint）";
        }
        return result;
    }
}
