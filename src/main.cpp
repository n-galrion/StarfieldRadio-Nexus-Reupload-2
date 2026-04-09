#include "SFSE/SFSE.h"
#include "fmt/format.h"
#include "RadioPlayer.h"
#include "DXHook.h"

#include <Mmsystem.h>
#include <mciapi.h>
#pragma comment(lib, "Winmm.lib")

#include <algorithm>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static bool gIsInitialized = false;

// ============================================================
// Utility functions
// ============================================================
std::wstring to_wstring(const std::string& str)
{
    int num_chars = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstrTo;
    if (num_chars) {
        wstrTo.resize(num_chars);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0], num_chars);
    }
    return wstrTo;
}

std::string utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string trimTrailingCommas(const std::string& str) {
    std::string result = str;
    result.erase(result.find_last_not_of(" \t") + 1);
    if (!result.empty() && result.back() == ',') result.pop_back();
    return result;
}

void trimPlaylist(std::vector<std::string>& playlist) {
    for (auto& song : playlist) song = trimTrailingCommas(song);
}

std::wstring GetRuntimePath()
{
    static wchar_t appPath[4096] = {0};
    if (appPath[0]) return appPath;
    GetModuleFileName(GetModuleHandle(NULL), appPath, sizeof(appPath) / sizeof(wchar_t));
    return appPath;
}

const std::wstring& GetRuntimeDirectory()
{
    static std::wstring s_runtimeDirectory;
    if (s_runtimeDirectory.empty()) {
        std::wstring runtimePath = GetRuntimePath();
        auto lastSlash = runtimePath.rfind(L'\\');
        if (lastSlash != std::wstring::npos)
            s_runtimeDirectory = runtimePath.substr(0, lastSlash + 1);
    }
    return s_runtimeDirectory;
}

const std::wstring& GetConfigPath(std::wstring configPath)
{
    static std::wstring s_configPath;
    if (s_configPath.empty()) {
        std::wstring runtimePath = GetRuntimeDirectory();
        if (!runtimePath.empty()) {
            s_configPath = runtimePath + configPath;
            REX::INFO("config path = {}", utf8_encode(s_configPath).c_str());
        }
    }
    return s_configPath;
}

void ConsoleExecute(std::string command)
{
    static REL::Relocation<void**>                       BGSScaleFormManager{REL::ID(938528)};
    static REL::Relocation<void (*)(void*, const char*)> ExecuteCommand{REL::ID(113576)};
    ExecuteCommand(*BGSScaleFormManager, command.data());
}

void Notification(std::string Message)
{
    std::string Command = fmt::format("cgf \"Debug.Notification\" \"{}\"", Message);
    ConsoleExecute(Command);
}

// ============================================================
// Config loading
// ============================================================
void loadConfig(Config& config, ShowConfig& showConfig)
{
    auto file_data = toml::parse(utf8_encode(GetConfigPath(L"Data\\SFSE\\Plugins\\StarfieldGalacticRadio.toml")), toml::spec::v(1, 1, 0));

    const auto& data = [&]() -> const toml::value& {
        if (file_data.is_table()) {
            for (const auto& [key, val] : file_data.as_table()) {
                if (val.is_table()) return val;
            }
        }
        return file_data;
    }();

    config.autoStartRadio = toml::find_or<bool>(data, "AutoStartRadio", false);
    config.randomizeStartTime = toml::find_or<bool>(data, "RandomizeStartTime", false);
    if (data.contains("Playlist") && data.at("Playlist").is_array()) {
        config.playlist.clear();
        config.playlist = toml::find<std::vector<std::string>>(data, "Playlist");
    }
    config.toggleRadioKey = toml::find_or<int>(data, "ToggleRadioKey", 0x60);
    config.switchModeKey = toml::find_or<int>(data, "SwitchModeKey", 0x6D);
    config.volumeUpKey = toml::find_or<int>(data, "VolumeUpKey", 0x69);
    config.volumeDownKey = toml::find_or<int>(data, "VolumeDownKey", 0x66);
    config.nextStationKey = toml::find_or<int>(data, "NextStationKey", 0x68);
    config.previousStationKey = toml::find_or<int>(data, "PreviousStationKey", 0x67);
    config.seekForwardKey = toml::find_or<int>(data, "SeekForwardKey", 0x6A);
    config.seekBackwardKey = toml::find_or<int>(data, "SeekBackwardKey", 0x6F);
    config.openMenuKey = toml::find_or<int>(data, "OpenMenuKey", 0x77); // F8

    showConfig.ShowOnAir = toml::find_or<bool>(data, "ShowOnAir", true);
    showConfig.ShowOnOff = toml::find_or<bool>(data, "ShowOnOff", true);
    showConfig.ShowPlayAt = toml::find_or<bool>(data, "ShowPlayAt", true);
    showConfig.ShowConnecting = toml::find_or<bool>(data, "ShowConnecting", true);
    showConfig.ShowMediaFound = toml::find_or<bool>(data, "ShowMediaFound", true);
    showConfig.ShowVolume = toml::find_or<bool>(data, "ShowVolume", true);
    showConfig.ShowInit = toml::find_or<bool>(data, "ShowInit", true);
}

// ============================================================
// Main loop thread
// ============================================================
const int TimePerFrame = 50;

static DWORD MainLoop(void* unused)
{
    (void)unused;

    Config config;
    ShowConfig showConfig;
    loadConfig(config, showConfig);
    trimPlaylist(config.playlist);

    REX::DEBUG("Loaded config, waiting for player form...");
    while (!RE::TESForm::LookupByID(0x14))
        Sleep(1000);

    REX::DEBUG("Pre-Initialize RadioPlayer.");
    RadioPlayer Radio(config.playlist, config.autoStartRadio, config.randomizeStartTime, showConfig);
    Radio.Init();
    g_RadioPlayer = &Radio;
    REX::DEBUG("Post-Initialize RadioPlayer.");

    // Key hold flags
    bool holdFlags[9] = {};
    bool menuHoldFlag = false;

    for (;;) {
        // Process commands from UI thread
        Radio.ProcessPendingCommands();

        // Menu toggle key - only works if DX hook is initialized
        short menuKeyState = GetKeyState(config.openMenuKey);
        if (menuKeyState < 0) {
            if (!menuHoldFlag) {
                menuHoldFlag = true;
                if (DXHook::IsInitialized()) {
                    bool isOpen = DXHook::IsMenuOpen();
                    DXHook::SetMenuOpen(!isOpen);
                    // Pause/unpause is handled in the render thread after confirming menu visibility
                }
            }
        } else {
            menuHoldFlag = false;
        }

        // Skip keybind processing when menu is open
        if (DXHook::IsMenuOpen()) {
            Sleep(TimePerFrame);
            continue;
        }

        // Original keybind processing
        struct { int key; bool* hold; RadioPlayer::Command cmd; } binds[] = {
            {config.toggleRadioKey,     &holdFlags[0], RadioPlayer::Command::TogglePlayer},
            {config.switchModeKey,      &holdFlags[1], RadioPlayer::Command::ToggleMode},
            {config.volumeUpKey,        &holdFlags[2], RadioPlayer::Command::IncreaseVolume},
            {config.volumeDownKey,      &holdFlags[3], RadioPlayer::Command::DecreaseVolume},
            {config.nextStationKey,     &holdFlags[4], RadioPlayer::Command::NextStation},
            {config.previousStationKey, &holdFlags[5], RadioPlayer::Command::PrevStation},
            {config.seekForwardKey,     &holdFlags[6], RadioPlayer::Command::SeekForward},
            {config.seekBackwardKey,    &holdFlags[7], RadioPlayer::Command::SeekBackward},
        };

        for (auto& b : binds) {
            short state = GetKeyState(b.key);
            if (state < 0) {
                if (!*b.hold) {
                    *b.hold = true;
                    // Execute directly since we're on the MCI thread
                    switch (b.cmd) {
                        case RadioPlayer::Command::TogglePlayer:   Radio.TogglePlayer(); break;
                        case RadioPlayer::Command::ToggleMode:     Radio.ToggleMode(); break;
                        case RadioPlayer::Command::IncreaseVolume: Radio.IncreaseVolume(); break;
                        case RadioPlayer::Command::DecreaseVolume: Radio.DecreaseVolume(); break;
                        case RadioPlayer::Command::NextStation:    Radio.NextStation(); break;
                        case RadioPlayer::Command::PrevStation:    Radio.PrevStation(); break;
                        case RadioPlayer::Command::SeekForward:    Radio.Seek(10); break;
                        case RadioPlayer::Command::SeekBackward:   Radio.Seek(-10); break;
                        default: break;
                    }
                }
            } else {
                *b.hold = false;
            }
        }

        Sleep(TimePerFrame);
    }

    return 0;
}

// ============================================================
// SFSE event handling
// ============================================================
namespace SRUtility {
    template <typename T>
    T* GetMember(const void* base, std::ptrdiff_t offset) {
        auto address = std::uintptr_t(base) + offset;
        auto reloc = REL::Relocation<T*>(address);
        return reloc.get();
    }

    RE::BSTEventSource<RE::MenuOpenCloseEvent>* GetMenuEventSource(RE::UI* ui) {
        using type = RE::BSTEventSource<RE::MenuOpenCloseEvent>;
        return GetMember<type>(ui, 0x20);
    }
}

class OpenCloseSink :
    public REX::Singleton<OpenCloseSink>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    RE::EventResult ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (a_event.menuName == "HUDMenu" && a_event.opening && !gIsInitialized) {
            REX::INFO("Creating Input Thread");
            CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
            gIsInitialized = true;
        }
        return RE::EventResult::kContinue;
    }

    static void Init()
    {
        SRUtility::GetMenuEventSource(RE::UI::GetSingleton())->RegisterSink(GetSingleton());
    }
};

void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
{
    if (a_msg->type == SFSE::MessagingInterface::kPostLoad) {
        OpenCloseSink::Init();
    }
    // Install DX hook after game data has loaded (safer timing)
    if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad) {
        REX::INFO("Radio UI: PostDataLoad - installing DX hook");
        try {
            DXHook::Install();
        } catch (...) {
            REX::INFO("Radio UI: DX hook install threw exception");
        }
    }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
#ifndef NDEBUG
    while (!IsDebuggerPresent()) Sleep(100);
#endif

    SFSE::Init(a_sfse);
    REX::INFO("{} v{} loaded", PROJECT_NAME, PROJECT_VERSION);

    SFSE::AllocTrampoline(1 << 10);
    SFSE::GetMessagingInterface()->RegisterListener(MessageCallback);

    return true;
}
