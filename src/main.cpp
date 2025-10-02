//#include "DKUtil/Config.hpp"
//#include "DKUtil/Hook.hpp"
//#include "DKUtil/Logger.hpp"
#include "SFSE/SFSE.h"
#include "fmt/format.h"  // Ensure fmt is included

// For MCI
#include <Mmsystem.h>
#include <mciapi.h>
#pragma comment(lib, "Winmm.lib")

// Formatting, string and console
#include <codecvt>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

static bool gIsInitialized = false;

std::wstring to_wstring(const std::string &str)
{
    size_t num_chars = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), NULL, 0);
    std::wstring wstrTo;
    if (num_chars)
    {
        wstrTo.resize(num_chars);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), &wstrTo[0], num_chars);
    }
    return wstrTo;
}

std::string utf8_encode(const std::wstring& wstr)
{
  if (wstr.empty()) return std::string();
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
  return strTo;
}

// Function to trim trailing commas and spaces
std::string trimTrailingCommas(const std::string& str) {
    std::string result = str;
    // Remove trailing spaces
    result.erase(result.find_last_not_of(" \t") + 1);
    // Remove trailing commas
    if (!result.empty() && result.back() == ',') {
        result.pop_back();
    }
    return result;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// Function to trim all items in the playlist
void trimPlaylist(std::vector<std::string>& playlist) {
    for (auto& song : playlist) {
        song = trimTrailingCommas(song);
    }
}

int hexStringToInt(const std::string& hexStr) {
    int value = 0;
    std::stringstream ss;
    ss << std::hex << hexStr;  // Read the hex string
    ss >> value;               // Convert to integer
    return value;
}

std::wstring GetRuntimePath()
{
  static wchar_t appPath[4096] = { 0 };

  if(appPath[0])
    return appPath;

  GetModuleFileName(GetModuleHandle(NULL), appPath, sizeof(appPath)/sizeof(wchar_t));

  return appPath;
}

std::wstring GetRuntimeName()
{
  std::wstring appPath = GetRuntimePath();

  std::wstring::size_type slashOffset = appPath.rfind(L'\\');
  if(slashOffset == std::wstring::npos)
    return appPath;

  return appPath.substr(slashOffset + 1);
}

const std::wstring & GetRuntimeDirectory()
{
  static std::wstring s_runtimeDirectory;

  if(s_runtimeDirectory.empty())
  {
    std::wstring runtimePath = GetRuntimePath();

    // truncate at last slash
    std::wstring::size_type lastSlash = runtimePath.rfind(L'\\');
    if(lastSlash != std::wstring::npos) // if we don't find a slash something is VERY WRONG
    {
      s_runtimeDirectory = runtimePath.substr(0, lastSlash + 1);
    }
    else
    {
      REX::WARN("no slash in runtime path? ({})", utf8_encode(runtimePath).c_str());
    }
  }

  return s_runtimeDirectory;
}

const std::wstring & GetConfigPath(std::wstring configPath)
{
  static std::wstring s_configPath;

  if(s_configPath.empty())
  {
    std::wstring runtimePath = GetRuntimeDirectory();
    if(!runtimePath.empty())
    {
      s_configPath = runtimePath + configPath;

      REX::INFO("config path = {}", utf8_encode(s_configPath).c_str());
    }
  }

  return s_configPath;
}

void ConsoleExecute(std::string command)
{
  static REL::Relocation<void**>                       BGSScaleFormManager{ REL::ID(938528) };
  static REL::Relocation<void (*)(void*, const char*)> ExecuteCommand{ REL::ID(113576) };
  ExecuteCommand(*BGSScaleFormManager, command.data());
}

void Notification(std::string Message)
{
  std::string Command = fmt::format("cgf \"Debug.Notification\" \"{}\"", Message);
  ConsoleExecute(Command);
}

// Structure to hold the configuration data
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
    bool showOnAir = true;

};

struct ShowConfig {
  bool ShowOnAir = true;
  bool ShowInit = true;
  bool ShowOnOff = true;
  bool ShowPlayAt = true;
  bool ShowVolume = true;
  bool ShowConnecting = true;
  bool ShowMediaFound = true;
};

// Function to load the configuration from the TOML file
void loadConfig(Config& config, ShowConfig& showConfig) {

    auto data = toml::parse(utf8_encode(GetConfigPath(L"Data\\SFSE\\Plugins\\StarfieldGalacticRadio.toml")), toml::spec::v(1,1,0));
    config.autoStartRadio = toml::find_or<bool>(data, "AutoStartRadio", false);
    config.randomizeStartTime = toml::find_or<bool>(data, "RandomizeStartTime", false);
    if (data.contains("Playlist") && data.at("Playlist").is_array())
    {
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
    config.seekBackwardKey = toml::find_or<int>(data, "ToggleRadioKey", 0x6F);
  
    showConfig.ShowOnAir = toml::find_or<bool>(data, "ShowOnAir", true);
    showConfig.ShowOnOff = toml::find_or<bool>(data, "ShowOnOff", true);
    showConfig.ShowPlayAt = toml::find_or<bool>(data, "ShowPlayAt", true);
    showConfig.ShowConnecting = toml::find_or<bool>(data, "ShowConnecting", true);
    showConfig.ShowMediaFound = toml::find_or<bool>(data, "ShowMediaFound", true);
    showConfig.ShowVolume = toml::find_or<bool>(data, "ShowVolume", true);
    showConfig.ShowInit = toml::find_or<bool>(data, "ShowInit", true);

}

// Function to print the loaded configuration
void printConfig(const Config& config, const ShowConfig& show) {
  
  REX::INFO("{} - AutoStartRadio: {}", PROJECT_NAME, config.autoStartRadio);
  REX::INFO("{} - RandomizeStartTime: {}", PROJECT_NAME, config.randomizeStartTime);
  REX::INFO("{} - Playlist:", PROJECT_NAME);
  for (const auto& song : config.playlist)
  {
    REX::INFO("{} playlist item - {}", PROJECT_NAME, song);
  }
  REX::INFO("{} - ToggleRadioKey: 0x{:X}", PROJECT_NAME, config.toggleRadioKey);
  REX::INFO("{} - SwitchModeKey: 0x{:X}", PROJECT_NAME, config.switchModeKey);
  REX::INFO("{} - VolumeUpKey: 0x{:X}", PROJECT_NAME, config.volumeUpKey);
  REX::INFO("{} - VolumeDownKey: 0x{:X}", PROJECT_NAME, config.volumeDownKey);
  REX::INFO("{} - NextStationKey: 0x{:X}", PROJECT_NAME, config.nextStationKey);
  REX::INFO("{} - PreviousStationKey: 0x{:X}", PROJECT_NAME, config.previousStationKey);
  REX::INFO("{} - SeekForwardKey: 0x{:X}", PROJECT_NAME, config.seekForwardKey);
  REX::INFO("{} - SeekBackwardKey: 0x{:X}", PROJECT_NAME, config.seekBackwardKey);

  REX::INFO("{} - ShowInit: {}", PROJECT_NAME, show.ShowInit);
  REX::INFO("{} - ShowConnecting: {}", PROJECT_NAME, show.ShowConnecting);
  REX::INFO("{} - ShowOnAir: {}", PROJECT_NAME, show.ShowOnAir);
  REX::INFO("{} - ShowPlayAt: {}", PROJECT_NAME, show.ShowPlayAt);
  REX::INFO("{} - ShowMediaFound: {}", PROJECT_NAME, show.ShowMediaFound);
  REX::INFO("{} - ShowVolume: {}", PROJECT_NAME, show.ShowVolume);
  REX::INFO("{} - ShowOnOff: {}", PROJECT_NAME, show.ShowOnOff);
}


class RadioPlayer
{
public:
  RadioPlayer(const std::vector<std::string>& InStations, bool InAutoStart, bool InRandomizeStartTime, const ShowConfig& InShow) :
    AutoStart(InAutoStart),
    RandomizeStartTime(InRandomizeStartTime),
    Stations(InStations),
    Show(InShow)
  {
  }

  ~RadioPlayer()
  {
  }

  void Init()
  {
    srand(time(NULL));
    randStart = rand() % (3600);
    RadioStartTime = std::time(nullptr);

    REX::INFO("{} v{} - Initializing Starfield Radio Sound System -", PROJECT_NAME, PROJECT_VERSION);

    if (Stations.size() <= 0) {
      REX::INFO("{} v{} - No Stations Found, Starfield Radio Shutting Down -", PROJECT_NAME, PROJECT_VERSION);
      return;
    }

    REX::INFO("{} v{} - Starting Starfield Radio -", PROJECT_NAME, PROJECT_VERSION);

    if (!Stations.empty()) {
      REX::INFO("{} v{} - {} Stations Found, Starfield Radio operational -", PROJECT_NAME, PROJECT_VERSION, Stations.size());
      StationIndex = rand() % Stations.size();
      if (StationIndex >= Stations.size()) {
        REX::INFO("{} - Invalid StationIndex: {}, unable to select station.", PROJECT_NAME, StationIndex);
        return;
      }
    }

    std::pair<std::string, std::string> StationInfo = GetStationInfo(Stations[StationIndex]);

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
      // Лог или сообщение об ошибке
      REX::INFO("{} v{} - Station path is empty, unable to start playback -", PROJECT_NAME, PROJECT_VERSION);
    }

    if (!StationInfo.first.empty() && Show.ShowOnAir)
      Notification(std::format("On Air - {}", StationInfo.first));

    REX::INFO("{} v{} - Selected Station {}, AutoStart: {}, Mode: {} -", PROJECT_NAME, PROJECT_VERSION, Stations[StationIndex], AutoStart, Mode);

    if (AutoStart && Mode == 0) {
      IsStarted = true;
      //mciSendString(L"play sfradio repeat", NULL, 0, NULL);
      int trackLength = getTrackLength();
      REX::INFO("{} - Track length: {}", PROJECT_NAME, trackLength);

      // Проверка корректности значения trackLength
      if (trackLength > 0) {
        mciSendString(to_wstring(std::format("play sfradio from {} repeat", (randStart % trackLength))).c_str(), NULL, 0, NULL);
      } else {
        // Лог или сообщение об ошибке для отладки
        REX::INFO("{} - Invalid track length: {}, playback cannot start", PROJECT_NAME, trackLength);
      }
      //Notification(std::format("当前播放进度：{}%%", std::floor((static_cast<float>(randStart % getTrackLength()) * 100 / getTrackLength()) * 10) / 10.0f));
      if (Show.ShowPlayAt)
        Notification(std::format("Play at: {}%%", std::floor((static_cast<float>(randStart % getTrackLength())*100 / getTrackLength()) * 10) / 10.0f));
      mciSendString(L"setaudio sfradio volume to 0", NULL, 0, NULL);
    }

    //Notification("银河电台初始化完成");
    if (Show.ShowInit)
      Notification("Starfield Radio Initialized");
  }

  int32_t getElapsedTimeInSec()
  {
    std::time_t currentTime = std::time(nullptr);
    return static_cast<int>(currentTime - RadioStartTime);
  }

  int32_t getTrackLength()
  {
    std::wstring StatusBuffer;
    StatusBuffer.reserve(128);

    mciSendString(L"status sfradio length", StatusBuffer.data(), 128, NULL);

    // Convert Length to int
    int32_t TrackLength = std::stoi(StatusBuffer);
    StatusBuffer.clear();
    return TrackLength;
  }

  void SelectStation(int InStationIndex)
  {
    // Index out of bounds.
    if (Stations.size() <= InStationIndex)
      return;

    std::pair<std::string, std::string> StationInfo = GetStationInfo(Stations[InStationIndex]);

    //const std::string& StationName = StationInfo.first;
    const std::string& StationURL = StationInfo.second;

    mciSendString(L"close sfradio", NULL, 0, NULL);

    if (StationURL.contains("://")) {
      std::wstring OpenLocalFile = to_wstring(std::format("open {} type mpegvideo alias sfradio", StationURL));
      int result = mciSendString(OpenLocalFile.c_str(), NULL, 0, NULL);
      if (result != 0) {
        REX::INFO("{} - mciSendString failed with code: {}", PROJECT_NAME, result);
        return;
      }
      //Notification("正在连接至银河电台网络。由于跨星际传输，通讯可能存在延迟，请稍等。");
      if (Show.ShowConnecting)
        Notification("Connecting to Galactic Radio Network. Delay expected because of the inter-stellar communication.");
    } else {
      std::wstring OpenLocalFile = to_wstring(std::format("open \".\\Data\\SFSE\\Plugins\\StarfieldGalacticRadio\\tracks\\{}\" type mpegvideo alias sfradio", StationURL));
      //Notification("在当前设备上检测到本地媒体文件，现在进行播放。");
      if (Show.ShowMediaFound)
        Notification("Local media on your device found, playing right now.");
      REX::INFO("{} - Attempt to load file - {}", PROJECT_NAME, StationURL);
      int result = mciSendString(OpenLocalFile.c_str(), NULL, 0, NULL);
      if (result != 0) {
        REX::INFO("{} - mciSendString failed with code: {}", PROJECT_NAME, result);
        return;
      }
    }

    int32_t NewPosition = (getElapsedTimeInSec() % getTrackLength() + randStart) * 1000 % getTrackLength();

    if (!StationInfo.first.empty() && Show.ShowOnAir)
      Notification(std::format("On Air - {}", StationInfo.first));

    // mciSendString(L"play sfradio repeat", NULL, 0, NULL);
    //Notification(std::format("当前播放进度：{}%%", std::floor((static_cast<float>(NewPosition % getTrackLength()) * 100 / getTrackLength()) * 10) / 10.0f));
    if (Show.ShowPlayAt)
      Notification(std::format("Play at: {}%%", std::floor((static_cast<float>(NewPosition % getTrackLength()) * 100 / getTrackLength()) * 10) / 10.0f));
    mciSendString(to_wstring(std::format("play sfradio from {} repeat", NewPosition)).c_str(), NULL, 0, NULL);

    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)Volume));
    mciSendString(v.c_str(), NULL, 0, NULL);
  }

  void NextStation()
  {
    StationIndex = (StationIndex + 1) % Stations.size();

    SelectStation(StationIndex);
  }

  void PrevStation()
  {
    StationIndex = (StationIndex - 1);
    if (StationIndex < 0)
      StationIndex = Stations.size() - 1;
    SelectStation(StationIndex);
  }

  void SetVolume(float InVolume)
  {
    Volume = InVolume;
    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)InVolume));
    mciSendString(v.c_str(), NULL, 0, NULL);
  }

  void DecreaseVolume()
  {
    Volume = Volume - 25.0f;
    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)Volume));
    mciSendString(v.c_str(), NULL, 0, NULL);

    if (Show.ShowVolume)
      Notification(std::format("Volume {}", Volume));
  }

  void IncreaseVolume()
  {
    Volume = Volume + 25.0f;
    std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)Volume));
    mciSendString(v.c_str(), NULL, 0, NULL);

    if (Show.ShowVolume)
      Notification(std::format("Volume {}", Volume));
  }

  uint32_t GetTrackLength()
  {
    std::wstring StatusBuffer;
    StatusBuffer.reserve(128);

    mciSendString(L"status sfradio length", StatusBuffer.data(), 128, NULL);

    // Convert Length to int
    uint32_t TrackLength = std::stoi(StatusBuffer);

    return TrackLength;
  }

  void Seek(int32_t InSeconds)
  {
    // Get current position from MCI with mciSendString
    std::wstring StatusBuffer;
    StatusBuffer.reserve(128);

    mciSendString(L"status sfradio length", StatusBuffer.data(), 128, NULL);

    // Convert Length to int
    int32_t TrackLength = std::stoi(StatusBuffer);
    StatusBuffer.clear();
    StatusBuffer.reserve(128);
    mciSendString(L"status sfradio position", StatusBuffer.data(), 128, NULL);

    int32_t Position = 0;

    if (StatusBuffer.contains(L":")) {
      std::tm             t;
      std::wistringstream ss(StatusBuffer);
      ss >> std::get_time(&t, L"%H:%M:%S");
      Position = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    } else {
      Position = std::stoi(StatusBuffer);
    }

    int32_t NewPosition = Position + (InSeconds * 1000);

    if (NewPosition >= TrackLength)
      NewPosition = TrackLength - 1;

    mciSendString(to_wstring(std::format("play sfradio from {} repeat", (int)NewPosition)).c_str(), NULL, 0, NULL);
  }

  void TogglePlayer()
  {
    //ENABLE_DEBUG

    IsPlaying = !IsPlaying;
    if (!IsStarted && IsPlaying) {
      IsStarted = true;
      mciSendString(L"play sfradio repeat", NULL, 0, NULL);

      if (RandomizeStartTime) {
        uint32_t TrackLength = GetTrackLength();
        srand(time(NULL));
        uint32_t RandomTime = rand() % TrackLength;

        Seek(RandomTime);
      }
    }

    std::pair<std::string, std::string> StationInfo = GetStationInfo(Stations[StationIndex]);

    if (IsPlaying) {
      if (!StationInfo.first.empty() && Show.ShowOnAir)
        Notification(std::format("On Air - {}", StationInfo.first));
      else if (Show.ShowOnOff)
        Notification("Radio On");
    } else if (Show.ShowOnOff)
      Notification("Radio Off");

    if (Mode == 0) {
      std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)IsPlaying ? Volume : 0.0f));
      mciSendString(v.c_str(), NULL, 0, NULL);
    } else {
      if (!IsPlaying) {
        mciSendString(L"stop sfradio", NULL, 0, NULL);
      } else {
        mciSendString(L"play sfradio repeat", NULL, 0, NULL);
        if (RandomizeStartTime) {
          uint32_t TrackLength = GetTrackLength();
          srand(time(NULL));
          uint32_t RandomTime = rand() % TrackLength;

          Seek(RandomTime);
        }
        std::wstring v = to_wstring(std::format("setaudio sfradio volume to {}", (int)Volume));
        mciSendString(v.c_str(), NULL, 0, NULL);
      }
    }
  }

  // 0 = Radio, 1 = Podcast
  void ToggleMode()
  {
    Mode = ~Mode;

    // Handle Podcast vs Radio mode.
    // Podcast mode will actually stop the stream, while Radio mode just mutes it so that time passes when not listened to.
  }

  std::pair<std::string, std::string> GetStationInfo(const std::string& StationConfig)
  {
    std::string Station = StationConfig;
    size_t      Separator = Station.find("|");

    std::string StationName = Separator != std::string::npos ? Station.substr(0, Station.find("|")) : "";
    std::string StationURL = Separator != std::string::npos ? Station.substr(Station.find("|") + 1) : Station;

    return std::make_pair(StationName, StationURL);
  }

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
};

const int    TimePerFrame = 50;
static DWORD MainLoop(void* unused)
{
  (void)unused;

  //ENABLE_DEBUG

  REX::DEBUG("Input Loop Starting");
  
  std::string configFilename = "StarfieldGalacticRadio.toml";

  Config config; // Create a Config instance
  ShowConfig showConfig;
  loadConfig(config, showConfig); // Load configuration from file
  
  trimPlaylist(config.playlist);
  //printConfig(config, showConfig);


  REX::DEBUG("Loaded config, waiting for player form...");
  while (!RE::TESForm::LookupByID(0x14))
    Sleep(1000);

  REX::DEBUG("Pre-Initialize RadioPlayer.");

  RadioPlayer Radio(config.playlist, config.autoStartRadio, config.randomizeStartTime, showConfig);
  Radio.Init();

  REX::DEBUG("Post-Initialize RadioPlayer.");

  bool ToggleRadioHoldFlag = false;
  bool SwitchModeHoldFlag = false;
  bool VolumeUpHoldFlag = false;
  bool VolumeDownHoldFlag = false;
  bool NextStationHoldFlag = false;
  bool PrevStationHoldFlag = false;
  bool SeekForwardHoldFlag = false;
  bool SeekBackwardHoldFlag = false;

  for (;;) {
    // Get current key states
    short ToggleRadioKeyState = GetKeyState(config.toggleRadioKey);
    short SwitchModeKeyState = GetKeyState(config.switchModeKey);
    short VolumeUpKeyState = GetKeyState(config.volumeUpKey);
    short VolumeDownKeyState = GetKeyState(config.volumeDownKey);
    short NextStationKeyState = GetKeyState(config.nextStationKey);
    short PrevStationKeyState = GetKeyState(config.previousStationKey);
    short SeekForwardKeyState = GetKeyState(config.seekForwardKey);
    short SeekBackwardKeyState = GetKeyState(config.seekBackwardKey);
    
    // REX::INFO("ToggleRadioKeyState: {}\nSwitchModeKeyState: {}\nVolumeUpKeyState: {}\nVolumeDownKeyState: {}\nNextStationKeyState: {}\nPrevStationKeyState: {}\nSeekForwardKeyState: {}\nSeekBackwardKeyState: {}", ToggleRadioKeyState, SwitchModeKeyState, VolumeUpKeyState, VolumeDownKeyState, NextStationKeyState, PrevStationKeyState, SeekForwardKeyState, SeekBackwardKeyState);

    // Handle toggle logic based on key states
    if (ToggleRadioKeyState < 0) { // Key is pressed
      if (!ToggleRadioHoldFlag) {
        ToggleRadioHoldFlag = 1; // Set the hold flag
        Radio.TogglePlayer(); // Perform action
      }
    } else {
      ToggleRadioHoldFlag = 0; // Reset hold flag when key is released
    }

    if (SwitchModeKeyState < 0) {
      if (!SwitchModeHoldFlag) {
        SwitchModeHoldFlag = 1;
        Radio.ToggleMode();
      }
    } else {
      SwitchModeHoldFlag = 0;
    }

    if (VolumeUpKeyState < 0) {
      if (!VolumeUpHoldFlag) {
        VolumeUpHoldFlag = 1;
        Radio.IncreaseVolume();
      }
    } else {
      VolumeUpHoldFlag = 0;
    }

    if (VolumeDownKeyState < 0) {
      if (!VolumeDownHoldFlag) {
        VolumeDownHoldFlag = 1;
        Radio.DecreaseVolume();
      }
    } else {
      VolumeDownHoldFlag = 0;
    }

    if (NextStationKeyState < 0) {
      if (!NextStationHoldFlag) {
        NextStationHoldFlag = 1;
        Radio.NextStation();
      }
    } else {
      NextStationHoldFlag = 0;
    }

    if (PrevStationKeyState < 0) {
      if (!PrevStationHoldFlag) {
        PrevStationHoldFlag = 1;
        Radio.PrevStation();
      }
    } else {
      PrevStationHoldFlag = 0;
    }

    if (SeekForwardKeyState < 0) {
      if (!SeekForwardHoldFlag) {
        SeekForwardHoldFlag = 1;
        Radio.Seek(10);
      }
    } else {
      SeekForwardHoldFlag = 0;
    }

    if (SeekBackwardKeyState < 0) {
      if (!SeekBackwardHoldFlag) {
        SeekBackwardHoldFlag = 1;
        Radio.Seek(-10);
      }
    } else {
      SeekBackwardHoldFlag = 0;
    }

    // Delay to control frame rate
    Sleep(TimePerFrame);
  }

  return 0;
}

namespace SRUtility {
  template <typename T>
  T* GetMember(const void* base, std::ptrdiff_t offset) {
    auto address = std::uintptr_t(base) + offset;
    auto reloc = REL::Relocation<T*>(address);
    return reloc.get();
  };

  RE::BSTEventSource<RE::MenuOpenCloseEvent>* GetMenuEventSource(RE::UI* ui) {
    using type = RE::BSTEventSource<RE::MenuOpenCloseEvent>;
    return GetMember<type>(ui, 0x20);
  }
}

class OpenCloseSink:
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
  if (a_msg->type == SFSE::MessagingInterface::kPostLoad)
  {
    OpenCloseSink::Init();
  }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
#ifndef NDEBUG
  while (!IsDebuggerPresent()) {
    Sleep(100);
  }
#endif

  SFSE::Init(a_sfse);

  REX::INFO("{} v{} loaded", PROJECT_NAME, PROJECT_VERSION);

  SFSE::AllocTrampoline(1 << 10);

  SFSE::GetMessagingInterface()->RegisterListener(MessageCallback);

  return true;
}
