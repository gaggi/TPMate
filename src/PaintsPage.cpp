#include "Pages.h"

#include "ui/PageWindow.h"
#include "ui/RowEditors.h"
#include "ui/RowList.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr int kListId = 100;
    constexpr int kNumbersId = 200;
    constexpr int kSpecMapsId = 201;
    constexpr int kCarsExpandHeight = 44;
    constexpr wchar_t kExpandGlyph = L'\uE70D';
    constexpr wchar_t kCollapseGlyph = L'\uE70E';

    // Rows of the page; headers have no key.
    enum class Setting
    {
        None,
        Cars,
        Helmets,
        Suits,
        OnlyPresentDrivers,
        ParallelDownloads,
        RefreshOnCtrlR,
        CleanUpOnExit
    };

    class PaintsPage : public PageWindow
    {
    public:
        explicit PaintsPage(const PageContext& context)
            : PageWindow(context.headingFont, context.textFont), context_(context)
        {
        }

    private:
        void OnCreate() override
        {
            list_.Create(Instance(), Handle(), kListId, HeadingFont(), TextFont());
            editors_.Attach(list_, Instance(), TextFont());
            Refresh();
        }

        void OnSize(int width, int height) override
        {
            Place(list_.Handle(), 0, 0, std::min(width, 760), height);
            editors_.Position();
        }

        bool OnCommand(int id, int code, HWND) override
        {
            auto& settings = *context_.settings;
            if (id == kNumbersId || id == kSpecMapsId)
            {
                if (code != BN_CLICKED) return true;
                (id == kNumbersId ? settings.loadNumbers : settings.loadSpecMaps) = editors_.Checked(id);
                Changed();
                return true;
            }
            if (id != kListId) return false;
            if (code == RowList::kLayoutChanged) { editors_.Position(); return true; }
            const int index = list_.NotifiedRow();
            if (index < 0 || static_cast<size_t>(index) >= keys_.size()) return true;
            const Setting setting = keys_[static_cast<size_t>(index)];
            const bool on = list_.Rows()[static_cast<size_t>(index)].toggle == 1;
            if (setting == Setting::Cars && (code == RowList::kIconButton || code == RowList::kActivated))
            {
                carsExpanded_ = !carsExpanded_;
                Refresh();
                if (carsExpanded_) list_.ScrollIntoView(index);
                return true;
            }
            if (setting == Setting::ParallelDownloads && (code == RowList::kButton || code == RowList::kActivated))
            {
                ChooseParallelDownloads();
                return true;
            }
            if (code != RowList::kToggled) return true;
            switch (setting)
            {
            case Setting::Cars: settings.loadCars = on; break;
            case Setting::Helmets: settings.loadHelmets = on; break;
            case Setting::Suits: settings.loadSuits = on; break;
            case Setting::OnlyPresentDrivers: settings.onlyPresentDrivers = on; break;
            case Setting::RefreshOnCtrlR: settings.refreshOnTextureReload = on; break;
            case Setting::CleanUpOnExit: settings.deleteAfterSession = on; break;
            default: return true;
            }
            Changed();
            return true;
        }

        void ChooseParallelDownloads()
        {
            std::vector<std::wstring> items;
            for (int count = 1; count <= 10; ++count)
                items.push_back(count == 1 ? L"1 at a time" : std::to_wstring(count) + L" at a time");
            const int choice = ChooseFromMenu(items, static_cast<int>(context_.settings->maxConcurrentDownloads) - 1);
            if (choice < 0) return;
            context_.settings->maxConcurrentDownloads = static_cast<unsigned int>(choice + 1);
            Changed();
        }

        void Changed()
        {
            context_.settingsChanged();
            Refresh();
        }

        static std::wstring CarDetail(const AppSettings& settings)
        {
            if (settings.loadNumbers && settings.loadSpecMaps) return L"Liveries and decals, with numbers and spec maps.";
            if (settings.loadNumbers) return L"Liveries and decals, with numbers.";
            if (settings.loadSpecMaps) return L"Liveries and decals, with spec maps.";
            return L"Liveries and decals only.";
        }

        void Refresh()
        {
            const auto& settings = *context_.settings;
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

            header(L"What to download");
            toggle(Setting::Cars, L"Cars", CarDetail(settings), settings.loadCars);
            rows.back().iconButtons = {carsExpanded_ ? kCollapseGlyph : kExpandGlyph};
            rows.back().expandHeight = carsExpanded_ ? kCarsExpandHeight : 0;
            const int carsRow = static_cast<int>(rows.size()) - 1;
            toggle(Setting::Helmets, L"Helmets", L"", settings.loadHelmets);
            toggle(Setting::Suits, L"Suits", L"", settings.loadSuits);

            header(L"Which drivers");
            toggle(Setting::OnlyPresentDrivers, L"Only drivers on track",
                L"Your own car loads in the garage; other cars when they appear on track.", settings.onlyPresentDrivers);

            header(L"Downloads");
            {
                RowList::Row row;
                row.title = L"Parallel downloads";
                row.detail = std::to_wstring(settings.maxConcurrentDownloads) + L" at a time";
                row.button = L"Choose...";
                rows.push_back(std::move(row));
                keys_.push_back(Setting::ParallelDownloads);
            }
            toggle(Setting::RefreshOnCtrlR, L"Ctrl+R re-downloads paints",
                L"Pressing Ctrl+R in the simulator fetches fresh paints.", settings.refreshOnTextureReload);
            toggle(Setting::CleanUpOnExit, L"Clean up when iRacing exits",
                L"Deletes the paints TPMate downloaded.", settings.deleteAfterSession);
            list_.SetRows(std::move(rows));

            if (!carsExpanded_) { editors_.Clear(); return; }
            if (editors_.Row() != carsRow)
            {
                editors_.Begin(carsRow);
                editors_.Check(kNumbersId, L"Numbers", settings.loadNumbers, 0, 8, 140);
                editors_.Check(kSpecMapsId, L"Spec maps", settings.loadSpecMaps, 150, 8, 160);
            }
            // Numbers and spec maps are layers of the car paint.
            EnableWindow(editors_.Get(kNumbersId), settings.loadCars);
            EnableWindow(editors_.Get(kSpecMapsId), settings.loadCars);
            editors_.Position();
        }

        PageContext context_;
        RowList list_;
        RowEditors editors_;
        std::vector<Setting> keys_;
        bool carsExpanded_{};
    };
}

HWND CreatePaintsPage(const PageContext& context, HWND parent)
{
    return PageWindow::Show(std::make_unique<PaintsPage>(context), context.instance, parent, 520, 560);
}
