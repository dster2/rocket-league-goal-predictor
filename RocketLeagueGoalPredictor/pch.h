#pragma once

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include "bakkesmod/plugin/bakkesmodplugin.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IMGUI/imgui.h"
#include "IMGUI/imgui_rangeslider.h"
#include "IMGUI/imgui_searchablecombo.h"
#include "IMGUI/imgui_stdlib.h"

#include "logging.h"

// Additional include: C:\git\onnxruntime-win-x64-1.23.2\include;$(ProjectDir);%(AdditionalIncludeDirectories)
// Additional library: C:\git\onnxruntime-win-x64-1.23.2\lib;%(AdditionalLibraryDirectories)
// Input -> Additional dependencies: C:\git\onnxruntime-win-x64-1.23.2\lib\onnxruntime.lib