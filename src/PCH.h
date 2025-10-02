#pragma once

#include <RE/Starfield.h>
#include <SFSE/SFSE.h>

#include <string>
#include <shlobj.h>
#include <stdint.h>
#include <toml.hpp>

using namespace std::literals;

#define DLLEXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
