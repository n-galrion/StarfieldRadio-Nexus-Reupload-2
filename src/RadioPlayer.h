#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <ctime>

// Forward declarations to avoid pulling in heavy headers here
void ConsoleExecute(std::string command);
void Notification(std::string Message);
std::wstring to_wstring(const std::string& str);

struct ShowConfig {
    bool ShowOnAir = true;
    bool ShowInit = true;
    bool ShowOnOff = true;
    bool ShowPlayAt = true;
    bool ShowVolume = true;
    bool ShowConnecting = true;
    bool ShowMediaFound = true;
};

struct Config {
    bool autoStartRadio = true;
    bool randomizeStartTime = true;
    std::vector<std::string> playlist;
    int toggleRadioKey = 0x60;
    int switchModeKey = 0x6D;
    int volumeUpKey = 0x69;
    int volumeDownKey = 0x66;
    int nextStationKey = 0x68;
    int previousStationKey = 0x67;
    int seekForwardKey = 0x6A;
    int seekBackwardKey = 0x6F;
    int openMenuKey = 0x77; // F8
    bool showOnAir = true;
};

class RadioPlayer
{
public:
    RadioPlayer(const std::vector<std::string>& InStations, bool InAutoStart, bool InRandomizeStartTime, const ShowConfig& InShow);
    ~RadioPlayer();

    void Init();
    void TogglePlayer();
    void ToggleMode();
    void NextStation();
    void PrevStation();
    void SelectStation(int InStationIndex);
    void SetVolume(float InVolume);
    void IncreaseVolume();
    void DecreaseVolume();
    void Seek(int32_t InSeconds);

    // Getters for UI
    int GetStationIndex() const { return StationIndex; }
    float GetVolume() const { return Volume; }
    bool GetIsPlaying() const { return IsPlaying; }
    int GetMode() const { return Mode; }
    const std::vector<std::string>& GetStations() const { return Stations; }

    std::pair<std::string, std::string> GetStationInfo(const std::string& StationConfig);
    std::vector<std::string> GetStationNames();

    uint32_t GetTrackLength();
    int32_t getTrackLength();
    int32_t getElapsedTimeInSec();

    // Thread-safe command queue for UI -> RadioPlayer communication
    enum class Command { None, TogglePlayer, NextStation, PrevStation, IncreaseVolume, DecreaseVolume, SeekForward, SeekBackward, ToggleMode };
    void PostCommand(Command cmd);
    void PostSelectStation(int index);
    void PostSetVolume(float vol);
    void ProcessPendingCommands();

private:
    int   Mode = 0;
    int   StationIndex = 0;
    float Volume = 700.0f;
    float Seconds = 0.0f;
    bool  RandomizeStartTime = false;
    bool  AutoStart = true;
    bool  IsStarted = false;
    bool  IsPlaying = false;

    std::vector<std::string> Stations;
    std::time_t              RadioStartTime;
    int32_t                  randStart;

    ShowConfig Show;

    // Command queue (atomic for lock-free single-producer single-consumer)
    std::atomic<Command> pendingCommand{Command::None};
    std::atomic<int>     pendingStationIndex{-1};
    std::atomic<float>   pendingVolume{-1.0f};
};

// Global radio player pointer (set by MainLoop, read by ImGui)
extern RadioPlayer* g_RadioPlayer;
