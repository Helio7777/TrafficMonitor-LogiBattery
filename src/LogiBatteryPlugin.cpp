#include "TrafficMonitorPluginABI.h"
#include "LogitechHidpp.h"

#include <algorithm>
#include <cwchar>
#include <mutex>
#include <string>

namespace
{
    using logibattery::BatterySnapshot;
    using logibattery::PowerStatus;

    class LogiBatteryItem final : public IPluginItem
    {
    public:
        const wchar_t* GetItemName() const override { return L"Logi 鼠标电量"; }
        const wchar_t* GetItemId() const override { return L"LogiMouseBatteryV1"; }
        const wchar_t* GetItemLableText() const override { return L"Logi:"; }
        const wchar_t* GetItemValueSampleText() const override { return L"100%+"; }

        const wchar_t* GetItemValueText() const override
        {
            std::lock_guard lock(mutex_);
            return value_.c_str();
        }

        void Update(const BatterySnapshot& s)
        {
            std::lock_guard lock(mutex_);
            if (!s.online || s.percent < 0)
            {
                value_ = L"N/A";
                usage_ = 0.0f;
                return;
            }

            value_ = std::to_wstring(std::clamp(s.percent, 0, 100)) + L"%";
            if (s.status == PowerStatus::Charging)
                value_ += L"+";
            else if (s.status == PowerStatus::Full)
                value_ += L"=";

            usage_ = static_cast<float>(std::clamp(s.percent, 0, 100)) / 100.0f;
        }

        // Set this to 1 if you want TrafficMonitor to draw its resource graph
        // behind the item.  Kept off by default for a clean battery readout.
        int IsDrawResourceUsageGraph() const override { return 0; }
        float GetResourceUsageGraphValue() const override { return usage_; }

    private:
        mutable std::mutex mutex_;
        std::wstring value_ = L"--";
        float usage_ = 0.0f;
    };

    class LogiBatteryPlugin final : public ITMPlugin
    {
    public:
        static LogiBatteryPlugin& Instance()
        {
            static LogiBatteryPlugin instance;
            return instance;
        }

        IPluginItem* GetItem(int index) override
        {
            return index == 0 ? &item_ : nullptr;
        }

        void DataRequired() override
        {
            service_.Start();
            const auto snapshot = service_.GetSnapshot();
            item_.Update(snapshot);

            std::lock_guard lock(textMutex_);
            tooltip_ = BuildTooltip(snapshot);
        }

        const wchar_t* GetInfo(PluginInfoIndex index) override
        {
            switch (index)
            {
            case TMI_NAME:        return L"Logi Mouse Battery";
            case TMI_DESCRIPTION: return L"在 TrafficMonitor 中显示 Logitech/Logi HID++ 鼠标电量。原生 Win32 HID，无需运行 LGSTrayBattery。";
            case TMI_AUTHOR:      return L"OpenAI / user project";
            case TMI_COPYRIGHT:   return L"GPL-3.0-or-later; HID++ implementation based on public protocol behavior and LGSTrayBattery references";
            case TMI_VERSION:     return L"1.0.0";
            case TMI_URL:         return L"https://github.com/andyvorld/LGSTrayBattery";
            default:              return L"";
            }
        }

        const wchar_t* GetTooltipInfo() override
        {
            std::lock_guard lock(textMutex_);
            return tooltip_.c_str();
        }

        int GetCommandCount() override { return 1; }

        const wchar_t* GetCommandName(int command_index) override
        {
            return command_index == 0 ? L"立即刷新 Logi 鼠标电量" : nullptr;
        }

        void OnPluginCommand(int command_index, void*, void*) override
        {
            if (command_index == 0)
                service_.RequestRefresh();
        }

        void OnInitialize(ITrafficMonitor*) override
        {
            service_.Start();
        }

    private:
        static std::wstring BuildTooltip(const BatterySnapshot& s)
        {
            if (!s.online)
            {
                std::wstring text = L"Logi Mouse Battery\n未检测到支持 HID++ 2.0 电量功能的 Logitech 鼠标";
                if (!s.error.empty())
                    text += L"\n" + s.error;
                return text;
            }

            std::wstring text = s.deviceName.empty() ? L"Logitech Mouse" : s.deviceName;
            text += L"\n电量: " + std::to_wstring(s.percent) + L"%";

            switch (s.status)
            {
            case PowerStatus::Charging:    text += L"（充电中）"; break;
            case PowerStatus::Full:        text += L"（已充满）"; break;
            case PowerStatus::Discharging: text += L"（使用电池）"; break;
            case PowerStatus::NotCharging: text += L"（未充电）"; break;
            default: break;
            }

            if (s.milliVolts > 0)
                text += L"\n电压: " + std::to_wstring(s.milliVolts) + L" mV";
            if (!s.source.empty())
                text += L"\nHID++ 特性: " + s.source;
            return text;
        }

        LogiBatteryPlugin() = default;
        ~LogiBatteryPlugin() { service_.Stop(); }

        LogiBatteryItem item_;
        logibattery::LogitechBatteryService service_;
        std::mutex textMutex_;
        std::wstring tooltip_ = L"Logi Mouse Battery\n正在检测鼠标…";
    };
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &LogiBatteryPlugin::Instance();
}
