#pragma comment(lib, "pluginsdk.lib")
#include "pch.h"
#include "GoalPredictor.h"
#include "utils.h"
#include "version.h"
#include <future>

BAKKESMOD_PLUGIN(GoalPredictor, "Goal Predictor", stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH), PLUGINTYPE_SPECTATOR | PLUGINTYPE_REPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

const std::string MODEL_FILE_NAME = "goal_predictor_model_3v3.onnx";

const double PREDICTION_OVERLAP_RADIUS_MS = 30; // Cap prediction frame rate (in game time) to just over 30 FPS (replays are limited to 30 FPS anyway).
const OverlapOptions BALL_HIT_EVENT_OVERLAP_OPTIONS = { .overlapRadiusMs = 250, .onlyLookForEqual = true };

const double LOG_FREQUENCY_MS = 1000;

template <typename T>
inline void GoalPredictor::AddEvent(const T& event, OverlapOptions options) {
	gameDataTracker.AddEvent(GetCurrentGameTimeMs(gameWrapper), event, options);
}

inline bool GoalPredictor::IsActive(bool assertGameLive) {
	if (!*enabled || !currentGameKey.IsActive()) {
		return false;
	}
	auto server = gameWrapper->GetCurrentGameState();
	if (!server || server.IsNull()) {
		return false;
	}

	return !assertGameLive || server.GetbRoundActive();
}

void GoalPredictor::onLoad() {
	_globalCvarManager = cvarManager;

	gameWrapper->SetTimeout([this](GameWrapper* gameWrapper) {
		cvarManager->executeCommand("togglemenu " + GetMenuName());
	}, 1);

	LoadCVars();
	LoadModel();
	LoadEventHooks();
	LoadRenderer();

	ResetLocalState();
}

void GoalPredictor::onUnload() {
	// This waits for any pending futures to complete.
	ResetLocalState();

	inferenceEngine.Deinitialize();
}

void GoalPredictor::LoadCVars() {
	enabledCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_Enabled", "1", "Enable plugin", true, true, 0, true, 1));
	enabled = std::make_shared<bool>(enabledCvar->getBoolValue());
	enabledCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*enabled = newCvar.getBoolValue();
		ResetLocalState();
	});

	showTitleBarCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_ShowTitleBar", "1", "Show Title Bar", true, true, 0, true, 1));
	showTitleBar = std::make_shared<bool>(showTitleBarCvar->getBoolValue());
	showTitleBarCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*showTitleBar = newCvar.getBoolValue();
	});

	opacityPctCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_Opacity", std::to_string(DEFAULT_OPACITY), "Background Opacity %", true, true, (float)MIN_OPACITY, true, (float)MAX_OPACITY));
	opacityPct = std::make_shared<int>(opacityPctCvar->getIntValue());
	opacityPctCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*opacityPct = newCvar.getIntValue();
	});

	graphHistoryMsCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_GraphHistoryMs", std::to_string(DEFAULT_GRAPH_HISTORY), "Graph History (milliseconds)", true, true, (float)MIN_GRAPH_HISTORY, true, (float)MAX_GRAPH_HISTORY));
	graphHistoryMs = std::make_shared<int>(graphHistoryMsCvar->getIntValue());
	graphHistoryMsCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*graphHistoryMs = newCvar.getIntValue();
	});

	augmentationCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_Augmentation", std::to_string((int)DEFAULT_AUGMENTATION), "Model inference augmentation", true, true, 1, true, 4));
	augmentation = std::make_shared<Augmentation>((Augmentation)augmentationCvar->getIntValue());
	augmentationCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*augmentation = (Augmentation)newCvar.getIntValue();
	});

	logPredictionTimeCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_LogPredictionTime", "0", "Log Prediction Time", true, true, 0, true, 1));
	logPredictionTime = std::make_shared<bool>(logPredictionTimeCvar->getBoolValue());
	logPredictionTimeCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*logPredictionTime = newCvar.getBoolValue();
	});

	logInputsCvar = std::make_shared<CVarWrapper>(
		cvarManager->registerCvar("GoalPredictor_LogInputs", "0", "Log Model Inputs", true, true, 0, true, 1));
	logInputs = std::make_shared<bool>(logInputsCvar->getBoolValue());
	logInputsCvar->addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
		*logInputs = newCvar.getBoolValue();
	});
}

void GoalPredictor::LoadModel() {
	auto modelPath = (gameWrapper->GetDataFolder() / MODEL_FILE_NAME).string();
	bool modelLoadSuccess = inferenceEngine.Initialize(modelPath);
	if (modelLoadSuccess) {
		LOG("Goal Predictor Model loaded and tested successfully!");
	}
	else {
		LOG("Failed to load Goal Predictor Model! Plugin disabled, use `plugin reload goalpredictor` to try again.");
		return;
	}
}

void GoalPredictor::LoadEventHooks() {
	gameWrapper->HookEventWithCaller<ServerWrapper>("Function TAGame.GameEvent_Soccar_TA.OnGameTimeUpdated", [this](ServerWrapper server, void* params, std::string eventName) {
		if (!IsActive(true)) {
			return;
		}

		// Hopping around the replay can give incorrectly timed events, so allow replacing to minimum-seen time if the user back-tracks.
		AddEvent(SecondEvent(server.GetSecondsRemaining(), static_cast<bool>(server.GetbOverTime())), { .overlapRadiusMs = 1200, .onlyLookForEqual = true, .overlapAction = REPLACE_IF_EARLIER });
	});

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.EventHitBall", [this](CarWrapper car, void* params, std::string eventName) {
		if (!IsActive(true) || !car || car.IsNull()) {
			return;
		}

		AddEvent(BallHitEvent(car.GetPRI().GetTeamNum() == 1), BALL_HIT_EVENT_OVERLAP_OPTIONS);
	});

	// Sometimes this fires when the above doesn't for some reason, so we need it. And this won't fire if a teammate touches next, so we need the above.
	// And often enough *neither* fires on a clear ball touch for some reason, and I couldn't find anything usable in the Function Scanner for those cases...
	gameWrapper->HookEventWithCaller<ActorWrapper>("Function TAGame.Ball_TA.OnHitTeamNumChanged", [this](ActorWrapper ball, void* params, std::string eventName) {
		if (!IsActive(true)) {
			return;
		}

		AddEvent(BallHitEvent(gameWrapper->GetCurrentGameState().GetBall().GetHitTeamNum() == 1), BALL_HIT_EVENT_OVERLAP_OPTIONS);
	});

	gameWrapper->HookEventWithCaller<ActorWrapper>("Function VehiclePickup_Boost_TA.Idle.EndState", [this](ActorWrapper actor, void* params, std::string eventName) {
		if (!IsActive(true)) {
			return;
		}

		auto bigBoostIndex = InferenceEngine::GetBigBoostIndex(actor.GetLocation());
		if (bigBoostIndex.has_value()) {
			AddEvent(BigBoostPickupEvent(bigBoostIndex.value()), { .overlapRadiusMs = 200, .onlyLookForEqual = true });
		}
	});

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.EventDemolished", [this](CarWrapper victim, void* params, std::string eventName) {
		if (!IsActive(true) || !victim || victim.IsNull()) {
			return;
		}

		// It seems the victim.GetPRI() link has already been detached by now, but luckily we can search for the backref pri.GetCar() which still points to the victim.
		auto pris = gameWrapper->GetCurrentGameState().GetPRIs();
		for (int i = 0; i < pris.Count(); i++) {
			PriWrapper pri = pris.Get(i);
			if (!pri || pri.IsNull() || pri.GetTeamNum() > 1) {
				continue;
			}

			auto car = pri.GetCar();
			if (car && car.memory_address == victim.memory_address) {
				AddEvent(DemolitionEvent(pri.GetTeamNum() == 1, GetId(pri)), { .overlapRadiusMs = 200, .onlyLookForEqual = true });

				return;
			}
		}
	});

	gameWrapper->HookEvent("Function GameEvent_Soccar_TA.Countdown.BeginState", [this](std::string eventName) {
		if (!IsActive(false)) {
			return;
		}

		AddEvent(KickoffEvent(), { .overlapRadiusMs = 1000 });
	});

	gameWrapper->HookEvent("Function GameEvent_Soccar_TA.ReplayPlayback.BeginState", [this](std::string eventName) {
		inGoalReplay = true;
	});

	gameWrapper->HookEvent("Function GameEvent_Soccar_TA.ReplayPlayback.EndState", [this](std::string eventName) {
		inGoalReplay = false;
	});

	gameWrapper->HookEventWithCaller<ServerWrapper>("Function TAGame.GameEvent_Soccar_TA.TriggerGoalScoreEvent", [this](ServerWrapper caller, void* paramsPtr, std::string eventName) {
		if (!IsActive(false) || inGoalReplay) {
			return;
		}

		struct TriggerGoalScoreEventParams {
			int32_t TeamScoredOn;
			uintptr_t Scorer;
		};
		TriggerGoalScoreEventParams* p = (TriggerGoalScoreEventParams*)paramsPtr;

		if (p->TeamScoredOn == 0 || p->TeamScoredOn == 1) {
			AddEvent(GoalEvent(p->TeamScoredOn == 0), { .overlapRadiusMs = 200 });
		}
	});

	gameWrapper->HookEvent("Function Engine.GameViewportClient.Tick", [this](std::string eventName) {
		GameKey gameKey = GetGameKey(gameWrapper);
		if (gameKey != currentGameKey) {
			ResetLocalState(gameKey);
		}
		if (!gameKey.IsActive()) {
			return;
		}

		// Handle any prediction tasks that have completed.
		auto completedPredictions = pendingPredictions.GetCompletedTasks();
		for (const auto& [timeMs, prediction] : completedPredictions) {
			if (prediction.has_value()) {
				gameDataTracker.AddEvent<Prediction>(
					timeMs,
					prediction.value(),
					{ .overlapRadiusMs = PREDICTION_OVERLAP_RADIUS_MS, .overlapAction = REPLACE }
				);
			}
		}
		if (!completedPredictions.empty()) {
			LogPredictionTime();
		}

		// Update time tracking
		auto currentGameTimeMs = GetCurrentGameTimeMs(gameWrapper);
		auto newGameTime = currentGameTimeMs != lastGameTimeMs;
		auto currentWorldTimeMs = GetCurrentWorldTimeMs(gameWrapper);
		auto newWorldTime = currentWorldTimeMs != lastTickWorldTimeMs;

		if (currentGameTimeMs != lastGameTimeMs) {
			lastGameTimeWorldTimeMs = currentWorldTimeMs;
			lastTickWorldTimeMs = currentWorldTimeMs;
		}
		if (!gameWrapper->IsPaused()) {
			lastTickWorldTimeMs = currentWorldTimeMs;
		}
		lastGameTimeMs = currentGameTimeMs; 

		// If the game is active, and we're at a new time, continue to consider making a new prediction.
		if (!IsActive() || (!newGameTime && !newWorldTime)) {
			return;
		}

		// Make a prediction, as long as there's no existing nearby predictions.
		// In practice there should only be 0 or 1 existing Predictions in this range, but let's be defensive
		auto overlapRange = gameDataTracker.GetRangeAroundInclusive<Prediction>(currentGameTimeMs, PREDICTION_OVERLAP_RADIUS_MS);
		// Check overlap in pending predictions too
		auto closestPendingMs = pendingPredictions.GetClosestTimeMs(currentGameTimeMs);
		bool alreadyScheduled = closestPendingMs.has_value() &&
			std::abs(closestPendingMs.value() - currentGameTimeMs) <= PREDICTION_OVERLAP_RADIUS_MS;
		if (!overlapRange.empty() || alreadyScheduled) {
			return;
		}

		// Prepare the inputs to the model using the game objects which the prediction thread can't read from safely.
		auto input = inferenceEngine.GetInferenceInput(gameWrapper->GetCurrentGameState(), gameDataTracker, currentGameTimeMs, ShouldLogInputs());
		if (!input) {
			return;
		}

		// Start the async prediction task and store it in our watcher set.
		pendingPredictions.Add(currentGameTimeMs, std::async(
			std::launch::async,
			[this, input = std::move(input.value())]() {
				return inferenceEngine.Predict(input, *augmentation);
			}
		));
	});
}

void GoalPredictor::ResetLocalState(GameKey newGameKey) {
	gameDataTracker.Clear();
	pendingPredictions.WaitAllAndClear();
	currentGameKey = newGameKey;

	lastGameTimeMs = -1;
	lastGameTimeWorldTimeMs = -1;
	lastTickWorldTimeMs = -1;
	inGoalReplay = false;
}

void GoalPredictor::LogPredictionTime() {
	if (!*logPredictionTime) {
		return;
	}

	static double lastLogPredictionTimeEpochTimeMs = 0;
	double currentEpochTimeMs = GetCurrentEpochTimeMs();
	if (currentEpochTimeMs - lastLogPredictionTimeEpochTimeMs <= LOG_FREQUENCY_MS) {
		return;
	}
	lastLogPredictionTimeEpochTimeMs = currentEpochTimeMs;

	auto predictions = gameDataTracker.GetRangeInclusive<Prediction>(lastGameTimeMs - 1000, lastGameTimeMs);
	if (predictions.empty()) {
		return;
	}

	double totalPredictionTimeMs = 0;
	int numPredictions = 0;
	for (auto const& [timeMs, prediction] : predictions) {
		totalPredictionTimeMs += prediction.prediction_time_ms;
		numPredictions += 1;
	}
	auto averagePredictionTimeMs = totalPredictionTimeMs / numPredictions;

	LOG("Average prediction time: {:.1f} ms", averagePredictionTimeMs);
}

bool GoalPredictor::ShouldLogInputs() {
	if (!*logInputs) {
		return false;
	}

	static double lastLogInputsEpochTimeMs = 0;
	double currentEpochTimeMs = GetCurrentEpochTimeMs();
	if (currentEpochTimeMs - lastLogInputsEpochTimeMs > LOG_FREQUENCY_MS) {
		lastLogInputsEpochTimeMs = currentEpochTimeMs;
		return true;
	}
	else {
		return false;
	}
}
