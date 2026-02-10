#pragma once
#include "bakkesmod/wrappers/Engine/WorldInfoWrapper.h"
#include "bakkesmod/wrappers/GameWrapper.h"
#include "GameEvents.h"
#include <memory>

inline static double GetCurrentWorldTimeMs(std::shared_ptr<GameWrapper> gameWrapper) {
	return gameWrapper->GetCurrentGameState().GetWorldInfo().GetTimeSeconds() * 1000.0;
}

inline static double GetCurrentGameTimeMs(std::shared_ptr<GameWrapper> gameWrapper) {
	if (gameWrapper->IsInReplay()) {
		return  (double)gameWrapper->GetGameEventAsReplay().GetReplayTimeElapsed() * 1000.0;
	}
	else if (gameWrapper->IsInOnlineGame()) {
		return GetCurrentWorldTimeMs(gameWrapper);
	}
	else {
		return 0;
	}
}

inline static int GetCurrentReplayFrame(std::shared_ptr<GameWrapper> gameWrapper) {
    if (gameWrapper->IsInReplay()) {
        return  gameWrapper->GetGameEventAsReplay().GetCurrentReplayFrame();
    }
    else {
        return -1;
    }
}

inline static double GetCurrentEpochTimeMs() {
	return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline static std::string GetId(PriWrapper pri) {
    return pri.GetUniqueIdWrapper().GetIdString();
}

inline static std::pair<Vector, Vector> RotatorToRotAndUpVectors(const Rotator R) {
    Vector rot;
    Vector up;

    float fPitch = R.Pitch * static_cast<float>(CONST_UnrRotToRad);
    float fYaw = R.Yaw * static_cast<float>(CONST_UnrRotToRad);
    float fRoll = R.Roll * static_cast<float>(CONST_UnrRotToRad);

    float SinPitch = sinf(fPitch);
    float CosPitch = cosf(fPitch);
    float SinYaw = sinf(fYaw);
    float CosYaw = cosf(fYaw);
    float SinRoll = sinf(fRoll);
    float CosRoll = cosf(fRoll);

    rot.X = CosPitch * CosYaw;
    rot.Y = CosPitch * SinYaw;
    rot.Z = SinPitch;

    up.X = -CosYaw * SinPitch * CosRoll - SinYaw * SinRoll;
    up.Y = -SinYaw * SinPitch * CosRoll + CosYaw * SinRoll;
    up.Z = CosPitch * CosRoll;

    return { rot, up };
}

inline static bool IsSpectatingOnline(std::shared_ptr<GameWrapper> gameWrapper) {
    if (!gameWrapper->IsInOnlineGame()) {
        return false;
    }

    auto playerController = gameWrapper->GetPlayerController();
    if (!playerController || playerController.IsNull()) {
        return false;
    }

    auto pri = playerController.GetPRI();
    return pri && !pri.IsNull() && pri.GetTeamNum() > 1;
}

inline static bool Is3v3(ServerWrapper server) {
    auto PRIs = server.GetPRIs();
    int num_team0_found = 0;
    int num_team1_found = 0;
    int num_car_nulls = 0;
    for (int i = 0; i < PRIs.Count(); ++i) {
        PriWrapper pri = PRIs.Get(i);
        if (pri.IsNull() || pri.IsSpectator()) {
            continue;
        }

        if (pri.GetTeamNum() == 0) {
            num_team0_found += 1;
        }
        else if (pri.GetTeamNum() == 1) {
            num_team1_found += 1;
        }
    }

    return num_team0_found == 3 && num_team1_found == 3;
}

// Don't know a clean way, so apply some imperfect heuristics...
inline static bool IsSoccar(ServerWrapper server) {
    auto ball = server.GetBall();
    if (!ball || ball.IsNull() || ball.IsDropshotBall()) {
        return false;
    }

    auto goals = server.GetGoals();
    if (goals.Count() != 2) {
        return false;
    }
    auto goalLoc = goals.Get(0).GetLocation(); // Should be (0, +/-5120, 312)
    if (std::abs(goalLoc.X) + std::abs(std::abs(goalLoc.Y) - 5120) > 10) {
        return false;
    }

    return true;
}

inline static GameKey GetGameKey(std::shared_ptr<GameWrapper> gameWrapper) {
    GameKey gameKey;

    if (gameWrapper->IsInReplay()) {
        gameKey.type = REPLAY;
    }
    else if (IsSpectatingOnline(gameWrapper)) {
        gameKey.type = ONLINE;
    }
    else {
        return {};
    }

    auto server = gameWrapper->GetCurrentGameState();
    if (!server || server.IsNull() || !Is3v3(server) || !IsSoccar(server)) {
        return {};
    }

    gameKey.guid = server.GetMatchGUID();

    return gameKey;
}