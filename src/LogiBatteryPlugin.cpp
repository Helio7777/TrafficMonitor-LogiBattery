#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif
#include "TrafficMonitorPluginABI.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "LogitechHidpp.h"
#include "MchoseHid.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>

#pragma comment(lib, "comctl32.lib")

#ifndef LOGIBATTERY_PROJECT_URL
#define LOGIBATTERY_PROJECT_URL L""
#endif

namespace
{
    using mousebattery::BatterySnapshot;
    using mousebattery::PowerStatus;

    enum class DeviceBrand : int
    {
        Logitech = 0,
        Mchose = 1,
    };

    const wchar_t* BrandDisplayName(DeviceBrand brand)
    {
        return brand == DeviceBrand::Mchose ? L"迈从 / MCHOSE" : L"Logitech / Logi";
    }

    class MouseBatteryItem final : public IPluginItem
    {
    public:
        const wchar_t* GetItemName() const override
        {
            return L"鼠标电量 (Logitech / MCHOSE)";
        }

        // Keep the v1 item ID so upgrading does not reset TrafficMonitor's
        // existing display/color configuration for this item.
        const wchar_t* GetItemId() const override { return L"LogiMouseBatteryV1"; }

        const wchar_t* GetItemLableText() const override
        {
            return brand_.load() == DeviceBrand::Mchose ? L"MCHOSE:" : L"Logi:";
        }

        const wchar_t* GetItemValueSampleText() const override { return L"100%+"; }

        const wchar_t* GetItemValueText() const override
        {
            // TrafficMonitor consumes the returned pointer after this call.  Copy
            // into per-thread storage so a concurrent DataRequired() update cannot
            // invalidate the string buffer.
            thread_local std::wstring copy;
            std::lock_guard lock(mutex_);
            copy = value_;
            return copy.c_str();
        }

        void SetBrand(DeviceBrand brand)
        {
            brand_.store(brand);
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

        int IsDrawResourceUsageGraph() const override { return 0; }
        float GetResourceUsageGraphValue() const override
        {
            std::lock_guard lock(mutex_);
            return usage_;
        }

    private:
        std::atomic<DeviceBrand> brand_{ DeviceBrand::Logitech };
        mutable std::mutex mutex_;
        std::wstring value_ = L"--";
        float usage_ = 0.0f;
    };

    class MouseBatteryPlugin final : public ITMPlugin
    {
    public:
        static MouseBatteryPlugin& Instance()
        {
            static MouseBatteryPlugin instance;
            return instance;
        }

        IPluginItem* GetItem(int index) override
        {
            return index == 0 ? &item_ : nullptr;
        }

        void DataRequired() override
        {
            BatterySnapshot snapshot;
            DeviceBrand selected = DeviceBrand::Logitech;
            {
                std::lock_guard lock(serviceMutex_);
                selected = brand_.load();
                if (selected == DeviceBrand::Mchose)
                {
                    mchoseService_.Start();
                    snapshot = mchoseService_.GetSnapshot();
                }
                else
                {
                    logitechService_.Start();
                    snapshot = logitechService_.GetSnapshot();
                }
            }

            item_.SetBrand(selected);
            item_.Update(snapshot);

            std::lock_guard lock(textMutex_);
            tooltip_ = BuildTooltip(snapshot, selected);
        }

        OptionReturn ShowOptionsDialog(void* hParent) override
        {
            const DeviceBrand before = brand_.load();
            int selectedRadio = before == DeviceBrand::Mchose ? 102 : 101;
            int pressedButton = IDCANCEL;

            const TASKDIALOG_BUTTON radios[] = {
                { 101, L"Logitech（HID++ 2.0）" },
                { 102, L"MCHOSE / 迈从（原生 HID）" },
            };

            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = static_cast<HWND>(hParent);
            config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
            config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
            config.pszWindowTitle = L"鼠标电量插件设置";
            config.pszMainInstruction = L"选择要读取的鼠标品牌";
            config.pszContent = L"插件会保存此设置，并立即使用对应的设备协议刷新电量。";
            config.cRadioButtons = static_cast<UINT>(std::size(radios));
            config.pRadioButtons = radios;
            config.nDefaultRadioButton = selectedRadio;

            const HRESULT hr = TaskDialogIndirect(
                &config, &pressedButton, &selectedRadio, nullptr);

            if (FAILED(hr))
            {
                const int fallback = MessageBoxW(
                    static_cast<HWND>(hParent),
                    L"请选择读取方式：\n\n“是” = Logitech\n“否” = MCHOSE / 迈从\n“取消” = 保持不变",
                    L"鼠标电量插件设置",
                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (fallback == IDCANCEL)
                    return OR_OPTION_UNCHANGED;
                selectedRadio = fallback == IDYES ? 101 : 102;
                pressedButton = IDOK;
            }

            if (pressedButton != IDOK)
                return OR_OPTION_UNCHANGED;

            const DeviceBrand after = selectedRadio == 102 ? DeviceBrand::Mchose : DeviceBrand::Logitech;
            if (after == before)
                return OR_OPTION_UNCHANGED;

            ApplyBrand(after, true);
            return OR_OPTION_CHANGED;
        }

        const wchar_t* GetInfo(PluginInfoIndex index) override
        {
            switch (index)
            {
            case TMI_NAME:        return L"Mouse Battery for TrafficMonitor";
            case TMI_DESCRIPTION: return L"显示 Logitech 与 MCHOSE 鼠标的电量、充电状态和连接信息。";
            case TMI_AUTHOR:      return L"TrafficMonitor Mouse Battery contributors";
            case TMI_COPYRIGHT:   return L"GPL-3.0-or-later; see THIRD_PARTY_NOTICES.md";
            case TMI_VERSION:     return L"1.1.2";
            case TMI_URL:         return LOGIBATTERY_PROJECT_URL;
            default:              return L"";
            }
        }

        const wchar_t* GetTooltipInfo() override
        {
            thread_local std::wstring copy;
            std::lock_guard lock(textMutex_);
            copy = tooltip_;
            return copy.c_str();
        }

        int GetCommandCount() override { return 1; }

        const wchar_t* GetCommandName(int command_index) override
        {
            return command_index == 0 ? L"立即刷新鼠标电量" : nullptr;
        }

        void OnPluginCommand(int command_index, void*, void*) override
        {
            if (command_index != 0)
                return;

            std::lock_guard lock(serviceMutex_);
            if (brand_.load() == DeviceBrand::Mchose)
                mchoseService_.RequestRefresh();
            else
                logitechService_.RequestRefresh();
        }

        void OnInitialize(ITrafficMonitor*) override
        {
            StartSelectedService();
        }

        void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override
        {
            if (index != EI_CONFIG_DIR || !data || !*data)
                return;

            {
                std::lock_guard lock(configMutex_);
                configDir_ = data;
            }

            const DeviceBrand loaded = LoadBrand();
            if (loaded != brand_.load())
                ApplyBrand(loaded, false);
        }

    private:
        static std::wstring BuildTooltip(const BatterySnapshot& s, DeviceBrand brand)
        {
            std::wstring text = BrandDisplayName(brand);
            text += L" 鼠标电量";

            if (!s.online)
            {
                text += brand == DeviceBrand::Mchose
                    ? L"\n未读取到 MCHOSE 电量；请唤醒设备后重试"
                    : L"\n未检测到支持 HID++ 2.0 电量功能的 Logitech 鼠标";
                if (!s.error.empty())
                    text += L"\n" + s.error;
                return text;
            }

            text += L"\n" + (s.deviceName.empty() ? std::wstring(L"Mouse") : s.deviceName);
            text += L"\n电量: " + std::to_wstring(s.percent) + L"%";

            switch (s.status)
            {
            case PowerStatus::Charging:    text += L"（充电中）"; break;
            case PowerStatus::Full:        text += L"（已充满）"; break;
            case PowerStatus::Discharging: text += L"（使用电池）"; break;
            case PowerStatus::NotCharging: text += L"（未充电）"; break;
            default: break;
            }

            if (!s.connectionMode.empty())
                text += L"\n连接: " + s.connectionMode;
            if (s.milliVolts > 0)
                text += L"\n电压: " + std::to_wstring(s.milliVolts) + L" mV";
            if (!s.source.empty())
                text += L"\n读取方式: " + s.source;
            return text;
        }

        std::wstring ConfigPath() const
        {
            std::lock_guard lock(configMutex_);
            if (configDir_.empty())
                return {};

            std::wstring path = configDir_;
            if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
                path += L'\\';
            path += L"LogiBatteryPlugin.ini";
            return path;
        }

        DeviceBrand LoadBrand() const
        {
            const std::wstring path = ConfigPath();
            if (path.empty())
                return brand_.load();

            wchar_t value[32]{};
            GetPrivateProfileStringW(
                L"MouseBattery", L"Brand", L"logitech", value,
                static_cast<DWORD>(std::size(value)), path.c_str());
            return (_wcsicmp(value, L"mchose") == 0) ? DeviceBrand::Mchose : DeviceBrand::Logitech;
        }

        void SaveBrand(DeviceBrand brand) const
        {
            const std::wstring path = ConfigPath();
            if (path.empty())
                return;
            WritePrivateProfileStringW(
                L"MouseBattery",
                L"Brand",
                brand == DeviceBrand::Mchose ? L"mchose" : L"logitech",
                path.c_str());
        }

        void StartSelectedService()
        {
            std::lock_guard lock(serviceMutex_);
            if (brand_.load() == DeviceBrand::Mchose)
                mchoseService_.Start();
            else
                logitechService_.Start();
        }

        void ApplyBrand(DeviceBrand brand, bool persist)
        {
            {
                std::lock_guard lock(serviceMutex_);
                const DeviceBrand old = brand_.load();
                if (old != brand)
                {
                    if (old == DeviceBrand::Mchose)
                        mchoseService_.Stop();
                    else
                        logitechService_.Stop();

                    brand_.store(brand);
                    item_.SetBrand(brand);
                    // Do not show a stale value from the previously selected brand
                    // while the new backend performs its first asynchronous query.
                    item_.Update(BatterySnapshot{});

                    if (brand == DeviceBrand::Mchose)
                        mchoseService_.RequestRefresh();
                    else
                        logitechService_.RequestRefresh();
                }
            }

            if (persist)
                SaveBrand(brand);

            std::lock_guard textLock(textMutex_);
            tooltip_ = std::wstring(BrandDisplayName(brand)) + L" 鼠标电量\n正在刷新…";
        }

        MouseBatteryPlugin() = default;
        ~MouseBatteryPlugin()
        {
            std::lock_guard lock(serviceMutex_);
            logitechService_.Stop();
            mchoseService_.Stop();
        }

        MouseBatteryItem item_;
        std::atomic<DeviceBrand> brand_{ DeviceBrand::Logitech };
        logibattery::LogitechBatteryService logitechService_;
        mousebattery::MchoseBatteryService mchoseService_;

        mutable std::mutex serviceMutex_;
        mutable std::mutex configMutex_;
        std::wstring configDir_;

        std::mutex textMutex_;
        std::wstring tooltip_ = L"Logitech 鼠标电量\n正在检测鼠标…";
    };
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &MouseBatteryPlugin::Instance();
}
