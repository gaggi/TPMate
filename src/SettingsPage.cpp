#include "Pages.h"

#include "ui/PageWindow.h"
#include "ui/RowList.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr int kListId = 100;
    constexpr wchar_t kOpenPageGlyph = L'\uE8A7';

    // Rows of the page; headers have no key.
    enum class Setting
    {
        None,
        MinimizeToTray,
        StartWithWindows,
        Version
    };

    class SettingsPage : public PageWindow
    {
    public:
        explicit SettingsPage(const PageContext& context)
            : PageWindow(context.headingFont, context.textFont), context_(context)
        {
        }

    private:
        void OnCreate() override
        {
            list_.Create(Instance(), Handle(), kListId, HeadingFont(), TextFont());
            Refresh();
        }

        void OnSize(int width, int height) override
        {
            Place(list_.Handle(), 0, 0, std::min(width, 760), height);
        }

        void OnTimer(UINT_PTR id) override
        {
            if (id == kPageRefreshTimer) Refresh();
        }

        bool OnCommand(int id, int code, HWND) override
        {
            if (id != kListId) return false;
            const int index = list_.NotifiedRow();
            if (index < 0 || static_cast<size_t>(index) >= keys_.size()) return true;
            const Setting setting = keys_[static_cast<size_t>(index)];
            const bool on = list_.Rows()[static_cast<size_t>(index)].toggle == 1;
            if (setting == Setting::MinimizeToTray && code == RowList::kToggled)
            {
                context_.settings->minimizeToTray = on;
                context_.settingsChanged();
            }
            else if (setting == Setting::StartWithWindows && code == RowList::kToggled)
                startupError_ = context_.setStartWithWindows(on);
            else if (setting == Setting::Version && code == RowList::kButton)
                VersionButton();
            else if (setting == Setting::Version && code == RowList::kIconButton)
                UpdateChecker::OpenReleasePage(context_.update->release.releasePageUrl);
            Refresh();
            return true;
        }

        void VersionButton()
        {
            const auto& update = *context_.update;
            if (update.phase != UpdateState::Phase::Available) context_.checkForUpdates();
            else if (!update.release.assetDownloadUrl.empty()) context_.installUpdate();
            else UpdateChecker::OpenReleasePage(update.release.releasePageUrl);
        }

        RowList::Row VersionRow() const
        {
            using Phase = UpdateState::Phase;
            const auto& update = *context_.update;
            RowList::Row row;
            row.title = L"Version " + UpdateChecker::CurrentVersion();
            row.button = L"Check now";
            switch (update.phase)
            {
            case Phase::Idle:
                row.detail = L"Look for a newer release on GitHub.";
                break;
            case Phase::Checking:
                row.detail = L"Checking GitHub...";
                row.buttonEnabled = false;
                break;
            case Phase::UpToDate:
                row.detail = update.message;
                row.pill = L"Up to date";
                row.pillTone = RowList::Tone::Active;
                break;
            case Phase::Available:
            {
                const bool installable = !update.release.assetDownloadUrl.empty();
                row.detail = L"Version " + update.release.versionDisplay + L" is available. " +
                    (installable ? L"TPMate restarts after installing it." : L"It has no download for this Windows version.");
                row.pill = L"Update available";
                row.pillTone = RowList::Tone::Accent;
                row.button = installable ? L"Install update" : L"Open release";
                if (installable && !update.release.releasePageUrl.empty()) row.iconButtons = {kOpenPageGlyph};
                break;
            }
            case Phase::Downloading:
                row.detail = L"Downloading the update...";
                row.button = L"Install update";
                row.buttonEnabled = false;
                break;
            case Phase::Failed:
                row.detail = update.message;
                row.pill = L"Not updated";
                row.pillTone = RowList::Tone::Warning;
                break;
            }
            return row;
        }

        void Refresh()
        {
            std::vector<RowList::Row> rows;
            keys_.clear();
            const auto header = [&](const wchar_t* title)
            {
                RowList::Row row;
                row.header = true;
                row.title = title;
                rows.push_back(std::move(row));
                keys_.push_back(Setting::None);
            };
            const auto toggle = [&](Setting key, const wchar_t* title, std::wstring detail, bool on)
            {
                RowList::Row row;
                row.title = title;
                row.detail = std::move(detail);
                row.toggle = on ? 1 : 0;
                rows.push_back(std::move(row));
                keys_.push_back(key);
            };

            header(L"Window");
            toggle(Setting::MinimizeToTray, L"Close and minimize to tray",
                L"TPMate keeps running in the notification area.", context_.settings->minimizeToTray);

            header(L"Startup");
            toggle(Setting::StartWithWindows, L"Start with Windows", startupError_.empty() ?
                L"Starts TPMate in the tray when you sign in. Windows asks for approval when this changes." :
                startupError_, *context_.startWithWindows);
            if (!startupError_.empty()) { rows.back().pill = L"Not changed"; rows.back().pillTone = RowList::Tone::Warning; }

            header(L"Updates");
            rows.push_back(VersionRow());
            keys_.push_back(Setting::Version);
            list_.SetRows(std::move(rows));
        }

        PageContext context_;
        RowList list_;
        std::vector<Setting> keys_;
        std::wstring startupError_;
    };
}

HWND CreateSettingsPage(const PageContext& context, HWND parent)
{
    return PageWindow::Show(std::make_unique<SettingsPage>(context), context.instance, parent, 520, 360);
}
