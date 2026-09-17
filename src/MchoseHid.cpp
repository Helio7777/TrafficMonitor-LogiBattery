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

#include "MchoseHid.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace mousebattery
{
    namespace
    {
        constexpr USHORT kLegacyMchoseVid = 0x3837;
        constexpr USHORT kG3AVid = 0xA8A5;
        constexpr USHORT kG3APid = 0x2255;
        constexpr USHORT kConfigUsagePage = 0xFF01;
        constexpr USHORT kG3AUsage = 0x0010;
        constexpr USHORT kWirelessPid = 0x100A;
        constexpr USHORT kWiredPid = 0x4018;
        constexpr BYTE kCommandReportId = 0x11;
        constexpr BYTE kInputReportId = 0x13;

        enum class MchoseProtocol
        {
            LegacyE2,
            G3A,
        };

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
            return h == INVALID_HANDLE_VALUE ? UniqueHandle(nullptr) : UniqueHandle(h);
        }

        struct MchoseEndpoint
        {
            std::wstring path;
            std::wstring productName;
            USHORT vendorId = 0;
            USHORT productId = 0;
            USHORT usage = 0;
            USHORT inputReportLength = 0;
            USHORT outputReportLength = 0;
            USHORT featureReportLength = 0;
            MchoseProtocol protocol = MchoseProtocol::LegacyE2;
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

        std::wstring ReadProductName(HANDLE handle)
        {
            wchar_t buffer[256]{};
            if (HidD_GetProductString(handle, buffer, sizeof(buffer)))
                return buffer;
            return L"MCHOSE Mouse";
        }

        int EndpointPriority(const MchoseEndpoint& endpoint)
        {
            if (endpoint.protocol == MchoseProtocol::G3A)
                return 0;
            // Same preference as dsh-mchose-battery: wired first because the
            // receiver can remain enumerated while the mouse is connected by USB.
            if (endpoint.productId == kWiredPid)
                return 1;
            if (endpoint.productId == kWirelessPid)
                return 2;
            return 3;
        }

        std::vector<MchoseEndpoint> EnumerateMchoseEndpoints()
        {
            std::vector<MchoseEndpoint> endpoints;

            GUID hidGuid{};
            HidD_GetHidGuid(&hidGuid);
            HDEVINFO devInfoSet = SetupDiGetClassDevsW(
                &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (devInfoSet == INVALID_HANDLE_VALUE)
                return endpoints;

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
                SetupDiGetDeviceInterfaceDetailW(
                    devInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
                if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                    continue;

                std::vector<BYTE> detailStorage(requiredSize);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailStorage.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                if (!SetupDiGetDeviceInterfaceDetailW(
                        devInfoSet, &interfaceData, detail, requiredSize, nullptr, nullptr))
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
                    (attributes.VendorID != kLegacyMchoseVid && attributes.VendorID != kG3AVid))
                    continue;

                PHIDP_PREPARSED_DATA preparsed = nullptr;
                if (!HidD_GetPreparsedData(static_cast<HANDLE>(device.get()), &preparsed))
                    continue;

                HIDP_CAPS caps{};
                const NTSTATUS capsStatus = HidP_GetCaps(preparsed, &caps);
                HidD_FreePreparsedData(preparsed);
                if (capsStatus != HIDP_STATUS_SUCCESS || caps.UsagePage != kConfigUsagePage)
                    continue;
                if (attributes.VendorID == kG3AVid &&
                    (attributes.ProductID != kG3APid || caps.Usage != kG3AUsage))
                    continue;

                MchoseEndpoint endpoint;
                endpoint.path = detail->DevicePath;
                endpoint.productName = ReadProductName(static_cast<HANDLE>(device.get()));
                endpoint.vendorId = attributes.VendorID;
                endpoint.productId = attributes.ProductID;
                endpoint.usage = caps.Usage;
                endpoint.inputReportLength = caps.InputReportByteLength;
                endpoint.outputReportLength = caps.OutputReportByteLength;
                endpoint.featureReportLength = caps.FeatureReportByteLength;
                endpoint.protocol = attributes.VendorID == kG3AVid &&
                    attributes.ProductID == kG3APid
                    ? MchoseProtocol::G3A
                    : MchoseProtocol::LegacyE2;
                endpoints.push_back(std::move(endpoint));
            }

            SetupDiDestroyDeviceInfoList(devInfoSet);

            std::stable_sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) {
                return EndpointPriority(a) < EndpointPriority(b);
            });
            return endpoints;
        }

        bool WriteReport(HANDLE handle,
                         const std::vector<uint8_t>& report,
                         DWORD timeoutMs,
                         HANDLE stopEvent)
        {
            UniqueHandle event = MakeHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event)
                return false;

            OVERLAPPED ov{};
            ov.hEvent = static_cast<HANDLE>(event.get());
            DWORD written = 0;
            BOOL ok = WriteFile(handle, report.data(), static_cast<DWORD>(report.size()), &written, &ov);
            if (!ok)
            {
                if (GetLastError() != ERROR_IO_PENDING)
                    return false;
                const IoWaitResult waitResult =
                    WaitOverlappedIo(handle, ov, timeoutMs, stopEvent, written);
                if (waitResult != IoWaitResult::Completed)
                    return false;
            }
            return written > 0;
        }

        bool WriteLegacyOutputReport(HANDLE handle,
                                     const MchoseEndpoint& endpoint,
                                     DWORD timeoutMs,
                                     HANDLE stopEvent)
        {
            if (endpoint.outputReportLength < 3)
                return false;

            std::vector<uint8_t> report(endpoint.outputReportLength, 0xFF);
            report[0] = kCommandReportId;
            report[1] = static_cast<uint8_t>(0x0B ^ 0xFF); // WebHID payload byte 0
            report[2] = static_cast<uint8_t>(0xAA ^ 0xFF); // WebHID payload byte 1
            return WriteReport(handle, report, timeoutMs, stopEvent);
        }

        bool WriteG3ABatteryReport(HANDLE handle,
                                   const MchoseEndpoint& endpoint,
                                   DWORD timeoutMs,
                                   HANDLE stopEvent)
        {
            if (endpoint.outputReportLength < 11)
                return false;

            // The official MCHOSE G-series page sends report ID 0 with this
            // 0x55/0x30/0xA5/0x0B/0x2E battery command.
            std::vector<uint8_t> report(endpoint.outputReportLength, 0);
            report[0] = 0x00;
            report[1] = 0x55;
            report[2] = 0x30;
            report[3] = 0xA5;
            report[4] = 0x0B;
            report[5] = 0x2E;
            report[6] = 0x01;
            report[7] = 0x01;
            report[8] = 0x01;
            return WriteReport(handle, report, timeoutMs, stopEvent);
        }

        bool SendReloadCommand(HANDLE handle, const MchoseEndpoint& endpoint, HANDLE stopEvent)
        {
            if (endpoint.protocol == MchoseProtocol::G3A)
                return WriteG3ABatteryReport(handle, endpoint, 300, stopEvent);

            // dsh-mchose-battery first calls sendFeatureReport(0x11, 64 bytes)
            // and falls back to sendReport.  Windows HidD_SetFeature includes the
            // report ID in byte 0, hence the +1 shape here.
            if (endpoint.featureReportLength >= 3)
            {
                std::vector<uint8_t> feature(endpoint.featureReportLength, 0xFF);
                feature[0] = kCommandReportId;
                feature[1] = static_cast<uint8_t>(0x0B ^ 0xFF);
                feature[2] = static_cast<uint8_t>(0xAA ^ 0xFF);
                if (HidD_SetFeature(handle, feature.data(), static_cast<ULONG>(feature.size())))
                    return true;
            }

            return WriteLegacyOutputReport(handle, endpoint, 300, stopEvent);
        }

        bool ReadOneReport(HANDLE handle,
                           const MchoseEndpoint& endpoint,
                           std::vector<uint8_t>& out,
                           DWORD timeoutMs,
                           HANDLE stopEvent)
        {
            // Windows HID expects reads sized to the collection's InputReportByteLength.
            // WebHID hides the report ID, while ReadFile includes it in byte 0.
            const size_t readLength = endpoint.inputReportLength >= 2
                ? static_cast<size_t>(endpoint.inputReportLength)
                : static_cast<size_t>(65);
            std::vector<uint8_t> buffer(readLength, 0);

            UniqueHandle event = MakeHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event)
                return false;

            OVERLAPPED ov{};
            ov.hEvent = static_cast<HANDLE>(event.get());
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, &ov);
            if (!ok)
            {
                if (GetLastError() != ERROR_IO_PENDING)
                    return false;
                const IoWaitResult waitResult =
                    WaitOverlappedIo(handle, ov, timeoutMs, stopEvent, bytesRead);
                if (waitResult != IoWaitResult::Completed)
                    return false;
            }

            if (bytesRead == 0)
                return false;
            buffer.resize(bytesRead);
            out = std::move(buffer);
            return true;
        }

        bool ParseE2Report(const std::vector<uint8_t>& report,
                           const MchoseEndpoint& endpoint,
                           BatterySnapshot& snapshot)
        {
            if (report.size() < 2 || report[0] != kInputReportId)
                return false;

            // WebHID's inputreport event exposes e.data without the report ID.
            // Recreate that payload and apply the project's XOR-0xFF decode.
            std::vector<uint8_t> payload(report.begin() + 1, report.end());
            for (auto& byte : payload)
                byte ^= 0xFF;

            if (payload.size() < 17 || payload[0] != 0xE2)
                return false;

            const int battery = payload[4];
            if (battery < 0 || battery > 100)
                return false;

            std::string asciiName;
            for (size_t i = 9; i < payload.size() && payload[i] != 0; ++i)
            {
                if (payload[i] >= 0x20 && payload[i] <= 0x7E)
                    asciiName.push_back(static_cast<char>(payload[i]));
            }

            std::wstring deviceName;
            if (!asciiName.empty())
                deviceName.assign(asciiName.begin(), asciiName.end());
            if (deviceName.empty())
                deviceName = endpoint.productName.empty() ? L"MCHOSE Mouse" : endpoint.productName;

            const bool charging = payload[3] != 0;
            snapshot.online = true;
            snapshot.deviceName = std::move(deviceName);
            snapshot.percent = battery;
            snapshot.status = charging ? PowerStatus::Charging
                                       : (battery >= 100 ? PowerStatus::Full : PowerStatus::Discharging);
            snapshot.source = L"MCHOSE E2 / HID report 0x13";
            if (endpoint.productId == kWiredPid)
                snapshot.connectionMode = L"USB";
            else if (endpoint.productId == kWirelessPid)
                snapshot.connectionMode = L"2.4G";
            else
                snapshot.connectionMode = L"HID";
            return true;
        }

        bool ParseG3ABatteryReport(const std::vector<uint8_t>& report,
                                   const MchoseEndpoint& endpoint,
                                   BatterySnapshot& snapshot)
        {
            // Windows ReadFile includes report ID at byte 0. The WebHID page
            // observes the remaining bytes as AA 30 ... battery charge.
            if (report.size() < 11 || report[0] != 0x00 ||
                report[1] != 0xAA || report[2] != 0x30)
                return false;

            const int battery = report[9];
            if (battery < 0 || battery > 100)
                return false;

            snapshot.online = true;
            snapshot.deviceName = endpoint.productName.empty()
                ? L"MCHOSE G3 A"
                : endpoint.productName;
            snapshot.percent = battery;
            snapshot.status = report[10] != 0
                ? PowerStatus::Charging
                : (battery >= 100 ? PowerStatus::Full : PowerStatus::Discharging);
            snapshot.connectionMode = L"2.4G";
            snapshot.source = L"MCHOSE G3 A / HID report 0x30";
            return true;
        }

        bool QueryEndpoint(const MchoseEndpoint& endpoint,
                           BatterySnapshot& snapshot,
                           HANDLE stopEvent)
        {
            UniqueHandle device = MakeHandle(CreateFileW(
                endpoint.path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr));
            if (!device)
                return false;

            HANDLE handle = static_cast<HANDLE>(device.get());
            if (!SendReloadCommand(handle, endpoint, stopEvent))
                return false;

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                    return false;

                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                const DWORD timeout = static_cast<DWORD>(std::clamp<long long>(remaining.count(), 1, 300));

                std::vector<uint8_t> report;
                if (!ReadOneReport(handle, endpoint, report, timeout, stopEvent))
                    continue;
                const bool parsed = endpoint.protocol == MchoseProtocol::G3A
                    ? ParseG3ABatteryReport(report, endpoint, snapshot)
                    : ParseE2Report(report, endpoint, snapshot);
                if (parsed)
                    return true;
            }
            return false;
        }
    }

    MchoseBatteryService::MchoseBatteryService()
    {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    MchoseBatteryService::~MchoseBatteryService()
    {
        Stop();
        if (stopEvent_)
        {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
    }

    void MchoseBatteryService::Start()
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

    void MchoseBatteryService::Stop()
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

    void MchoseBatteryService::RequestRefresh()
    {
        Start();
        {
            std::lock_guard lock(wakeMutex_);
            refreshRequested_ = true;
        }
        wakeCv_.notify_all();
    }

    BatterySnapshot MchoseBatteryService::GetSnapshot() const
    {
        std::lock_guard lock(snapshotMutex_);
        return snapshot_;
    }

    void MchoseBatteryService::WorkerMain()
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

    BatterySnapshot MchoseBatteryService::QueryOnce()
    {
        BatterySnapshot result;
        if (stopEvent_ && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0)
            return result;

        const auto endpoints = EnumerateMchoseEndpoints();
        if (endpoints.empty())
        {
            result.error = L"未找到 MCHOSE 配置 HID 接口（VID 0x3837，或 G3 A 的 A8A5:2255 / FF01:0010）。";
            return result;
        }

        for (const auto& endpoint : endpoints)
        {
            if (stopEvent_ && WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0)
                return result;
            BatterySnapshot candidate;
            if (QueryEndpoint(endpoint, candidate, stopEvent_))
                return candidate;
        }

        result.error = L"已检测到 MCHOSE 配置接口，但未收到可解码的电量报告；鼠标可能休眠或型号协议不同。";
        return result;
    }
}
