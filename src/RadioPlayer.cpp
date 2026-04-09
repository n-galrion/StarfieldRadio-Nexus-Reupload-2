#include "pch.h"
#include "RadioPlayer.h"
#include "fmt/format.h"
#include <Mmsystem.h>
#include <mciapi.h>
#pragma comment(lib, "Winmm.lib")

RadioPlayer* g_RadioPlayer = nullptr;

RadioPlayer::RadioPlayer(const std::vector<std::string>& InStations, bool InAutoStart, bool InRandomizeStartTime, const ShowConfig& InShow)
    : AutoStart(InAutoStart), RandomizeStartTime(InRandomizeStartTime), Stations(InStations), Show(InShow)
{
}

RadioPlayer::~RadioPlayer() {}

void RadioPlayer::Init()
{
    srand(static_cast<unsigned int>(time(NULL)));
    randStart = rand() % 3600;
    RadioStartTime = std::time(nullptr);

    REX::INFO("{} v{} - Initializing Starfield Radio Sound System -", PROJECT_NAME, PROJECT_VERSION);

    if (Stations.empty()) {
        REX::INFO("{} v{} - No Stations Found, Starfield Radio Shutting Down -", PROJECT_NAME, PROJECT_VERSION);
        return;
    }

    REX::INFO("{} v{} - {} Stations Found, Starfield Radio operational -", PROJECT_NAME, PROJECT_VERSION, Stations.size());
    StationIndex = rand() % static_cast<int>(Stations.size());
    if (StationIndex >= static_cast<int>(Stations.size())) {
        REX::INFO("{} - Invalid StationIndex: {}, unable to select station.", PROJECT_NAME, StationIndex);
        return;
    }

    auto StationInfo = GetStationInfo(Stations[StationIndex]);

    if (!StationInfo.second.empty()) {
        REX::INFO("{} - Attempt to load file - {}", PROJECT_NAME, StationInfo.second);
        std::wstring OpenLocalFile;
        if (StationInfo.second.find("://") != std::string::npos) {
            OpenLocalFile = to_wstring(std::format("open {} type mpegvideo alias sfradio", StationInfo.second));
        } else {
            OpenLocalFile = to_wstring(std::format("open \".\\Data\\SFSE\\Plugins\\StarfieldGalacticRadio\\tracks\\{}\" type mpegvideo alias sfradio", StationInfo.second));
        }
        int result = mciSendString(OpenLocalFile.c_str(), NULL, 0, NULL);
        if (result != 0) {
            REX::INFO("{} - mciSendString failed with code: {}", PROJECT_NAME, result);
            return;
        }
    } else {
        REX::INFO("{} v{} - Station path is empty, unable to start playback -", PROJECT_NAME, PROJECT_VERSION);
    }

    if (!StationInfo.first.empty() && Show.ShowOnAir)
        Notification(std::format("On Air - {}", StationInfo.first));

    REX::INFO("{} v{} - Selected Station {}, AutoStart: {}, Mode: {} -", PROJECT_NAME, PROJECT_VERSION, Stations[StationIndex], AutoStart, Mode);

    if (AutoStart && Mode == 0) {
        IsStarted = true;
        int trackLength = getTrackLength();
        REX::INFO("{} - Track length: {}", PROJECT_NAME, trackLength);

        if (trackLength > 0) {
            mciSendString(to_wstring(std::format("play sfradio from {} repeat", (randStart % trackLength))).c_str(), NULL, 0, NULL);
        } else {
            REX::INFO("{} - Invalid track length: {}, playback cannot start", PROJECT_NAME, trackLength);
        }
        if (Show.ShowPlayAt && trackLength > 0)
            Notification(std::format("Play at: {}%%", std::floor((static_cast<float>(randStart % trackLength) * 100 / trackLength) * 10) / 10.0f));
        mciSendString(L"setaudio sfradio volume to 0", NULL, 0, NULL);
    }

    if (Show.ShowInit)
        Notification("Starfield Radio Initialized");
}

int32_t RadioPlayer::getElapsedTimeInSec()
{
    std::time_t currentTime = std::time(nullptr);
    return static_cast<int>(currentTime - RadioStartTime);
}

int32_t RadioPlayer::getTrackLength()
{
    std::wstring StatusBuffer;
    StatusBuffer.reserve(128);
    mciSendString(L"status sfradio length", StatusBuffer.data(), 128, NULL);
    try { return std::stoi(StatusBuffer); } catch (...) { return 0; }
}

uint32_t RadioPlayer::GetTrackLength()
{
    return static_cast<uint32_t>(getTrackLength());
}

void RadioPlayer::SelectStation(int InStationIndex)
{
    if (InStationIndex < 0 || InStationIndex >= static_cast<int>(Stations.size()))
        return;

    auto StationInfo = GetStationInfo(Stations[InStationIndex]);
    const std::string& StationURL = StationInfo.second;

    mciSendString(L"close sfradio", NULL, 0, NULL);

    if (StationURL.contains("://")) {
        std::wstring OpenLocalFile = to_wstring(std::format("open {} type mpegvideo alias sfradio", StationURL));
        int result = mciSendString(OpenLocalFile.c_str(), NULL, 0, NULL);
        if (result != 0) { REX::INFO("{} - mciSendString failed with code: {}", PROJECT_NAME, result); return; }
        if (Show.ShowConnecting) Notification("Connecting to Galactic Radio Network...");
    } else {
        std::wstring OpenLocalFile = to_wstring(std::format("open \".\\Data\\SFSE\\Plugins\\StarfieldGalacticRadio\\tracks\\{}\" type mpegvideo alias sfradio", StationURL));
        if (Show.ShowMediaFound) Notification("Local media found, playing now.");
        int result = mciSendString(OpenLocalFile.c_str(), NULL, 0, NULL);
        if (result != 0) { REX::INFO("{} - mciSendString failed with code: {}", PROJECT_NAME, result); return; }
    }

    int32_t tl = getTrackLength();
    int32_t NewPosition = tl > 0 ? (getElapsedTimeInSec() % tl + randStart) * 1000 % tl : 0;

    if (!StationInfo.first.empty() && Show.ShowOnAir)
        Notification(std::format("On Air - {}", StationInfo.first));

    if (Show.ShowPlayAt && tl > 0)
        Notification(std::format("Play at: {}%%", std::floor((static_cast<float>(NewPosition % tl) * 100 / tl) * 10) / 10.0f));
    mciSendString(to_wstring(std::format("play sfradio from {} repeat", NewPosition)).c_str(), NULL, 0, NULL);

    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)Volume));
    mciSendString(v.c_str(), NULL, 0, NULL);

    StationIndex = InStationIndex;
}

void RadioPlayer::NextStation()
{
    StationIndex = (StationIndex + 1) % static_cast<int>(Stations.size());
    SelectStation(StationIndex);
}

void RadioPlayer::PrevStation()
{
    StationIndex = StationIndex - 1;
    if (StationIndex < 0) StationIndex = static_cast<int>(Stations.size()) - 1;
    SelectStation(StationIndex);
}

void RadioPlayer::SetVolume(float InVolume)
{
    Volume = InVolume;
    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)InVolume));
    mciSendString(v.c_str(), NULL, 0, NULL);
}

void RadioPlayer::DecreaseVolume()
{
    Volume = (std::max)(0.0f, Volume - 25.0f);
    SetVolume(Volume);
    if (Show.ShowVolume) Notification(std::format("Volume {}", (int)Volume));
}

void RadioPlayer::IncreaseVolume()
{
    Volume = (std::min)(1000.0f, Volume + 25.0f);
    SetVolume(Volume);
    if (Show.ShowVolume) Notification(std::format("Volume {}", (int)Volume));
}

void RadioPlayer::Seek(int32_t InSeconds)
{
    std::wstring StatusBuffer;
    StatusBuffer.reserve(128);
    mciSendString(L"status sfradio length", StatusBuffer.data(), 128, NULL);
    int32_t TrackLength = 0;
    try { TrackLength = std::stoi(StatusBuffer); } catch (...) { return; }

    StatusBuffer.clear();
    StatusBuffer.reserve(128);
    mciSendString(L"status sfradio position", StatusBuffer.data(), 128, NULL);

    int32_t Position = 0;
    if (StatusBuffer.contains(L":")) {
        std::tm t;
        std::wistringstream ss(StatusBuffer);
        ss >> std::get_time(&t, L"%H:%M:%S");
        Position = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    } else {
        try { Position = std::stoi(StatusBuffer); } catch (...) { return; }
    }

    int32_t NewPosition = std::clamp(Position + (InSeconds * 1000), 0, TrackLength - 1);
    mciSendString(to_wstring(std::format("play sfradio from {} repeat", NewPosition)).c_str(), NULL, 0, NULL);
}

void RadioPlayer::TogglePlayer()
{
    IsPlaying = !IsPlaying;
    if (!IsStarted && IsPlaying) {
        IsStarted = true;
        mciSendString(L"play sfradio repeat", NULL, 0, NULL);
        if (RandomizeStartTime) {
            uint32_t TrackLength = GetTrackLength();
            if (TrackLength > 0) {
                srand(static_cast<unsigned int>(time(NULL)));
                Seek(rand() % TrackLength);
            }
        }
    }

    if (Stations.empty() || StationIndex >= static_cast<int>(Stations.size()))
        return;

    auto StationInfo = GetStationInfo(Stations[StationIndex]);

    if (IsPlaying) {
        if (!StationInfo.first.empty() && Show.ShowOnAir)
            Notification(std::format("On Air - {}", StationInfo.first));
        else if (Show.ShowOnOff)
            Notification("Radio On");
    } else if (Show.ShowOnOff)
        Notification("Radio Off");

    if (Mode == 0) {
        std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", IsPlaying ? (int)Volume : 0));
        mciSendString(v.c_str(), NULL, 0, NULL);
    } else {
        if (!IsPlaying) {
            mciSendString(L"stop sfradio", NULL, 0, NULL);
        } else {
            mciSendString(L"play sfradio repeat", NULL, 0, NULL);
            if (RandomizeStartTime) {
                uint32_t TrackLength = GetTrackLength();
                if (TrackLength > 0) {
                    srand(static_cast<unsigned int>(time(NULL)));
                    Seek(rand() % TrackLength);
                }
            }
            SetVolume(Volume);
        }
    }
}

void RadioPlayer::ToggleMode()
{
    Mode = ~Mode;
}

std::pair<std::string, std::string> RadioPlayer::GetStationInfo(const std::string& StationConfig)
{
    size_t Separator = StationConfig.find("|");
    std::string StationName = Separator != std::string::npos ? StationConfig.substr(0, Separator) : "";
    std::string StationURL = Separator != std::string::npos ? StationConfig.substr(Separator + 1) : StationConfig;
    return {StationName, StationURL};
}

std::vector<std::string> RadioPlayer::GetStationNames()
{
    std::vector<std::string> names;
    for (const auto& s : Stations) {
        auto info = GetStationInfo(s);
        names.push_back(info.first.empty() ? info.second : info.first);
    }
    return names;
}

// Thread-safe command posting (from ImGui render thread)
void RadioPlayer::PostCommand(Command cmd)
{
    pendingCommand.store(cmd, std::memory_order_release);
}

void RadioPlayer::PostSelectStation(int index)
{
    pendingStationIndex.store(index, std::memory_order_release);
}

void RadioPlayer::PostSetVolume(float vol)
{
    pendingVolume.store(vol, std::memory_order_release);
}

// Called from MainLoop thread (same thread as MCI)
void RadioPlayer::ProcessPendingCommands()
{
    Command cmd = pendingCommand.exchange(Command::None, std::memory_order_acquire);
    switch (cmd) {
        case Command::TogglePlayer:    TogglePlayer(); break;
        case Command::NextStation:     NextStation(); break;
        case Command::PrevStation:     PrevStation(); break;
        case Command::IncreaseVolume:  IncreaseVolume(); break;
        case Command::DecreaseVolume:  DecreaseVolume(); break;
        case Command::SeekForward:     Seek(10); break;
        case Command::SeekBackward:    Seek(-10); break;
        case Command::ToggleMode:      ToggleMode(); break;
        default: break;
    }

    int stIdx = pendingStationIndex.exchange(-1, std::memory_order_acquire);
    if (stIdx >= 0) SelectStation(stIdx);

    float vol = pendingVolume.exchange(-1.0f, std::memory_order_acquire);
    if (vol >= 0.0f) SetVolume(vol);
}
