#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/wrappers/CVarWrapper.h"
#include "GameDataTracker.h"
#include "GameEvents.h"
#include "GuiBase.h"
#include "InferenceEngine.h"
#include "TimedTaskSet.h"

class GoalPredictor: public BakkesMod::Plugin::BakkesModPlugin, public PluginWindowBase, public SettingsWindowBase {
private:
	// CVars that are configurable in the Settings window
	std::shared_ptr<bool> enabled;
	std::shared_ptr<CVarWrapper> enabledCvar;

	std::shared_ptr<int> opacityPct;
	std::shared_ptr<CVarWrapper> opacityPctCvar;
	const int DEFAULT_OPACITY = 50;
	const int MIN_OPACITY = 0;
	const int MAX_OPACITY = 100;

	std::shared_ptr<bool> showTitleBar;
	std::shared_ptr<CVarWrapper> showTitleBarCvar;

	std::shared_ptr<int> graphHistoryMs;
	std::shared_ptr<CVarWrapper> graphHistoryMsCvar;
	const int DEFAULT_GRAPH_HISTORY = 5000;
	const int MIN_GRAPH_HISTORY = 1000;
	const int MAX_GRAPH_HISTORY = 9000;

	std::shared_ptr<Augmentation> augmentation;
	std::shared_ptr<CVarWrapper> augmentationCvar;
	const Augmentation DEFAULT_AUGMENTATION = AUGMENT_4X;

	// Hidden CVars that are only configurable in the BakkesMod console
	std::shared_ptr<bool> logPredictionTime; // GoalPredictor_LogPredictionTime
	std::shared_ptr<CVarWrapper> logPredictionTimeCvar;

	std::shared_ptr<bool> logInputs; // GoalPredictor_LogInputs
	std::shared_ptr<CVarWrapper> logInputsCvar;

	// State
	InferenceEngine inferenceEngine;
	GameKey currentGameKey;
	GameDataTracker gameDataTracker;
	TimedTaskSet<std::optional<Prediction>> pendingPredictions;

	// GameDataTracker uses the Game Time domain, but for replays that is low resolution (30 FPS) so would cause jittery
	// renders if used for graphing. Thus we track corresponding World Time (higher resolution) for the most recently
	// seen GameTime, as well as the most recent Tick overall, to smoothly slide the graph at true render FPS.
	// Using WorldTime instead of, say, EpochTime lets it naturally follow slower or faster replay playback speeds.
	// We can't use WorldTime for the GameDataTracker domain since it is always increasing, and for replays the user can
	// rewind, and we want to still use the existing data there, if applicable.
	double lastGameTimeMs; // last seen GameTimeMs value, in Game Time domain
	double lastGameTimeWorldTimeMs; // the WorldTimeMs value corresponding to when we first saw lastGameTimeMs in Tick()
	double lastTickWorldTimeMs; // the WorldTimeMs value of the last Tick() call
	bool inGoalReplay = false; // Replay of a goal during an online game, *not* related to watching a replay file

	void onLoad() override;
	void onUnload() override;

	void LoadCVars();
	void LoadModel();
	void LoadEventHooks();
	void LoadRenderer();

	template <typename T>
	inline void AddEvent(const T& event, OverlapOptions options = {});

	inline bool IsActive(bool assertGameLive = false);

	void ResetLocalState(GameKey newGameKey = GAME_KEY_NONE);
	void LogPredictionTime();
	bool ShouldLogInputs();

public:
	void RenderWindow() override;
	void RenderSettings() override;
};
