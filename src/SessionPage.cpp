#include "Pages.h"

#include "TextConvert.h"
#include "ui/PageWindow.h"
#include "ui/RowList.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
    constexpr int kListId = 100;

    enum class Action
    {
        None,
        OpenFolder,
        CleanFolder
    };

    std::wstring Count(size_t value, const wchar_t* singular, const wchar_t* plural)
    {
        return std::to_wstring(value) + L" " + (value == 1 ? singular : plural);
    }

    struct StatePill { const wchar_t* text; RowList::Tone tone; };

    StatePill PillFor(DriverPaintState state)
    {
        switch (state)
        {
        case DriverPaintState::NotOnTrack: return {L"Not on track", RowList::Tone::Neutral};
        case DriverPaintState::Waiting: return {L"Waiting", RowList::Tone::Neutral};
        case DriverPaintState::Checking: return {L"Checking", RowList::Tone::Accent};
        case DriverPaintState::Downloading: return {L"Downloading", RowList::Tone::Accent};
        case DriverPaintState::Installed: return {L"Installed", RowList::Tone::Active};
        case DriverPaintState::NoPaint: return {L"No paint", RowList::Tone::Neutral};
        case DriverPaintState::Failed: return {L"Failed", RowList::Tone::Warning};
        }
        return {L"", RowList::Tone::Neutral};
    }

    class SessionPage : public PageWindow
    {
    public:
        explicit SessionPage(const PageContext& context)
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
            if (index < 0 || static_cast<size_t>(index) >= actions_.size()) return true;
            if (code != RowList::kButton && code != RowList::kActivated) return true;
            const Action action = actions_[static_cast<size_t>(index)];
            // Both refresh this page through the main window when they are done.
            if (action == Action::OpenFolder) context_.openPaintFolder();
            else if (action == Action::CleanFolder) context_.cleanPaintFolder();
            return true;
        }

        void SessionRows(const PaintProgress& progress, std::vector<RowList::Row>& rows) const
        {
            const bool connected = *context_.connected;
            const auto add = [&](RowList::Row row) { rows.push_back(std::move(row)); };
            if (!connected || progress.rosterDrivers == 0)
            {
                RowList::Row row;
                row.title = connected ? L"Waiting for session info" : L"No session";
                row.detail = connected ? L"iRacing is loading the session." :
                    L"Join a session in iRacing; paints are downloaded automatically.";
                add(std::move(row));
                return;
            }
            {
                RowList::Row row;
                row.title = progress.trackName.empty() ? L"Current session" : Utf8ToWide(progress.trackName);
                row.detail = progress.carName.empty() ? L"" : L"Your car: " + Utf8ToWide(progress.carName);
                add(std::move(row));
            }
            {
                const bool onlyPresent = context_.settings->onlyPresentDrivers;
                RowList::Row row;
                row.title = L"Drivers";
                row.detail = onlyPresent ? L"Only drivers on track are loaded; others follow when they appear." :
                    L"Paints are loaded for every driver in the session.";
                row.pill = onlyPresent ? std::to_wstring(progress.selectedDrivers) + L" of " +
                    std::to_wstring(progress.rosterDrivers) + L" on track" : Count(progress.rosterDrivers, L"driver", L"drivers");
                row.pillTone = RowList::Tone::Accent;
                add(std::move(row));
            }
            {
                RowList::Row row;
                row.title = L"Paint files";
                if (progress.batchTotal > 0)
                {
                    row.detail = L"Downloading " + std::to_wstring(progress.batchDone) + L" of " + std::to_wstring(progress.batchTotal) + L".";
                    row.pill = L"Downloading";
                    row.pillTone = RowList::Tone::Accent;
                }
                else
                {
                    const size_t problems = progress.failedFiles + progress.failedLookups;
                    row.detail = Count(progress.installedFiles, L"file", L"files") + L" installed for this session";
                    if (progress.failedFiles) row.detail += L"  \u00B7  " + Count(progress.failedFiles, L"download", L"downloads") + L" failed";
                    if (progress.failedLookups) row.detail += L"  \u00B7  " + Count(progress.failedLookups, L"lookup", L"lookups") +
                        L" failed, retried with the next session update";
                    row.detail += problems ? L"; see Activity." : L".";
                    row.pill = problems ? std::to_wstring(problems) + L" failed" : std::to_wstring(progress.installedFiles) + L" installed";
                    row.pillTone = problems ? RowList::Tone::Warning :
                        progress.installedFiles ? RowList::Tone::Active : RowList::Tone::Neutral;
                }
                add(std::move(row));
            }
        }

        static void DriverRows(const PaintProgress& progress, std::vector<RowList::Row>& rows)
        {
            // Car names only help in multi-class sessions.
            std::set<std::string> cars;
            for (const auto& status : progress.drivers) cars.insert(status.driver.carName);
            for (const auto& status : progress.drivers)
            {
                const auto& driver = status.driver;
                RowList::Row row;
                row.title = (driver.carNumber.empty() ? L"" : L"#" + Utf8ToWide(driver.carNumber) + L"  ") +
                    (driver.userName.empty() ? L"Driver " + std::to_wstring(driver.userId) : Utf8ToWide(driver.userName));
                std::vector<std::wstring> details;
                if (status.player) details.push_back(L"You");
                if (!driver.teamName.empty() && driver.teamName != driver.userName) details.push_back(Utf8ToWide(driver.teamName));
                if (cars.size() > 1 && !driver.carName.empty()) details.push_back(Utf8ToWide(driver.carName));
                if (status.state == DriverPaintState::Installed && status.failedFiles > 0)
                    details.push_back(std::to_wstring(status.failedFiles) + L" of " +
                        std::to_wstring(status.installedFiles + status.failedFiles) + L" files failed");
                for (const auto& detail : details) row.detail += (row.detail.empty() ? L"" : L"  \u00B7  ") + detail;
                const StatePill pill = PillFor(status.state);
                row.pill = pill.text;
                row.pillTone = pill.tone;
                row.muted = status.state == DriverPaintState::NotOnTrack;
                rows.push_back(std::move(row));
            }
        }

        void Refresh()
        {
            std::vector<RowList::Row> rows;
            actions_.clear();
            const auto header = [&](const wchar_t* title)
            {
                RowList::Row row;
                row.header = true;
                row.title = title;
                rows.push_back(std::move(row));
            };

            const PaintProgress progress = context_.progress();
            header(L"Current session");
            SessionRows(progress, rows);
            if (*context_.connected && !progress.drivers.empty())
            {
                header(L"Drivers");
                DriverRows(progress, rows);
            }

            header(L"Paint folder");
            {
                RowList::Row row;
                row.title = L"Open paint folder";
                row.detail = L"Documents\\iRacing\\paint";
                if (!context_.openFolderMessage->empty()) row.detail = *context_.openFolderMessage;
                row.button = L"Open";
                rows.push_back(std::move(row));
            }
            {
                RowList::Row row;
                row.title = L"Clean iRacing paint folder";
                row.detail = context_.cleanFolderMessage->empty() ?
                    L"Moves all .tga and .mip files to the Recycle Bin. Asks first." : *context_.cleanFolderMessage;
                row.button = L"Clean...";
                rows.push_back(std::move(row));
            }
            actions_.assign(rows.size(), Action::None);
            actions_[rows.size() - 2] = Action::OpenFolder;
            actions_[rows.size() - 1] = Action::CleanFolder;
            list_.SetRows(std::move(rows));
        }

        PageContext context_;
        RowList list_;
        std::vector<Action> actions_;
    };
}

HWND CreateSessionPage(const PageContext& context, HWND parent)
{
    return PageWindow::Show(std::make_unique<SessionPage>(context), context.instance, parent, 520, 420);
}
