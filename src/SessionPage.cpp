#include "Pages.h"

#include "TextConvert.h"
#include "ui/PageWindow.h"
#include "ui/RowList.h"

#include <algorithm>
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
            if (action == Action::OpenFolder) context_.openPaintFolder();
            else if (action == Action::CleanFolder) context_.cleanPaintFolder();
            Refresh();
            return true;
        }

        void SessionRows(std::vector<RowList::Row>& rows) const
        {
            const PaintProgress progress = context_.progress();
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
                    row.detail = Count(progress.installedFiles, L"file", L"files") + L" installed for this session" +
                        (progress.failedFiles ? L"  \u00B7  " + Count(progress.failedFiles, L"download", L"downloads") + L" failed; see Activity." : L".");
                    row.pill = progress.failedFiles ? std::to_wstring(progress.failedFiles) + L" failed" :
                        std::to_wstring(progress.installedFiles) + L" installed";
                    row.pillTone = progress.failedFiles ? RowList::Tone::Warning : RowList::Tone::Active;
                }
                add(std::move(row));
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

            header(L"Current session");
            SessionRows(rows);

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
