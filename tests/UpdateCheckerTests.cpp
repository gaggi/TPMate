// Include the implementation to exercise version parsing without network requests.
#include "UpdateChecker.cpp"
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    const int helperResult = UpdateChecker::HandleSelfUpdateCommandLine();
    if (helperResult >= 0) return helperResult;
    if (argc == 2 && std::string_view(argv[1]) == "--wait") { Sleep(1500); return 0; }
    try
    {
        Require(UpdateChecker::CurrentVersion() == L"0.1.0", "Initial version");
        Require(IsNewerVersion(L"v0.1.1", L"0.1.0"), "Patch update with v prefix");
        Require(IsNewerVersion(L"0.10.0", L"0.9.0"), "Numeric version comparison");
        Require(!IsNewerVersion(L"0.1.0", L"0.1.0"), "Equal version");
        Require(!IsNewerVersion(L"0.0.9", L"0.1.0"), "Older version");
        Require(!IsNewerVersion(L"0.1.0-beta", L"0.1.0"), "Prerelease comparison");
        Require(ParseVersion(L"999999999999999999999").numbers.empty(), "Version overflow");
        const auto release = UpdateChecker::ParseReleaseMetadata(R"({"tag_name":"v0.2.0","html_url":"https://github.com/gaggi/TPMate/releases/tag/v0.2.0","assets":[{"name":"TPMate-windows-x86.exe","browser_download_url":"https://github.com/gaggi/TPMate/releases/download/v0.2.0/TPMate-windows-x86.exe"},{"name":"TPMate-windows-x64.exe","browser_download_url":"https://github.com/gaggi/TPMate/releases/download/v0.2.0/TPMate-windows-x64.exe"}]})");
        Require(release.state == UpdateCheckState::UpdateAvailable, "New release");
        Require(EndsWithInsensitive(release.release.assetName, PreferredAssetSuffix()), "Matching architecture");
        Require(UpdateChecker::ParseReleaseMetadata(R"({"tag_name":"v0.1.0"})").state == UpdateCheckState::UpToDate, "Current release");
        const auto noAsset = UpdateChecker::ParseReleaseMetadata(R"({"tag_name":"v0.2.0","assets":[]})");
        Require(noAsset.state == UpdateCheckState::UpdateAvailable && noAsset.release.assetDownloadUrl.empty(), "Release without matching asset");
        Require(UpdateChecker::ParseReleaseMetadata("invalid json").state == UpdateCheckState::Failed, "Malformed response");
        Require(UpdateChecker::ParseReleaseMetadata("[]").state == UpdateCheckState::Failed, "Wrong response shape");
        Require(UpdateChecker::ParseReleaseMetadata(R"({"tag_name":"invalid"})").state == UpdateCheckState::Failed, "Invalid tag");
        std::cout << "Update checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
