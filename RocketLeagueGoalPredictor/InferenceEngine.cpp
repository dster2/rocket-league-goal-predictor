#include "pch.h"
#include "InferenceEngine.h"
#include "logging.h"
#include "utils.h"
#include <algorithm>
#include <numbers>
#include <ranges>

const int INPUT_DIM = 114;
const int OUTPUT_DIM = 3;
const int NUM_BALL_COLS = 6;
const int NUM_PLAYER_COLS = 17;

const double BIG_BOOST_RESPAWN_PERIOD_MS = 10 * 1000;
const double PLAYER_RESPAWN_PERIOD_MS = 3 * 1000;
const double EVENT_LOOKUP_GRACE_MS = 100;
const double NEAR_ZERO_SECONDS_UNRELIABLE_THRESHOLD_SEC = 10;

const float DEG_TO_RAD = static_cast<float>(std::numbers::pi / 180);

void InferenceEngine::InitializeInternal(const std::string& model_path_str) {
    env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "GoalPredictor");

    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetInterOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    std::filesystem::path model_path = model_path_str;
    session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);

    size_t num_input_nodes = session->GetInputCount();
    input_node_names.reserve(num_input_nodes);
    input_node_names_ptr.reserve(num_input_nodes);

    for (size_t i = 0; i < num_input_nodes; i++) {
        auto input_name = session->GetInputNameAllocated(i, allocator);
        input_node_names.push_back(input_name.get());
        input_node_names_ptr.push_back(input_node_names.back().c_str());
    }

    size_t num_output_nodes = session->GetOutputCount();
    output_node_names.reserve(num_output_nodes);
    output_node_names_ptr.reserve(num_output_nodes);

    for (size_t i = 0; i < num_output_nodes; i++) {
        auto output_name = session->GetOutputNameAllocated(i, allocator);
        output_node_names.push_back(output_name.get());
        output_node_names_ptr.push_back(output_node_names.back().c_str());
    }

    InitializeMasks();
}

inline int player_col_index(int player_i, int player_col_i) {
    return NUM_BALL_COLS + NUM_PLAYER_COLS * player_i + player_col_i;
}

inline int boost_index(int boost_i) {
    return NUM_BALL_COLS + NUM_PLAYER_COLS * 6 + boost_i;
}

void InferenceEngine::InitializeMasks() {
    mask_flip_x.assign(INPUT_DIM, 1.0f);
    mask_flip_y.assign(INPUT_DIM, 1.0f);
    mask_flip_xy.assign(INPUT_DIM, 1.0f);

    // Apply logic to member variables
    mask_flip_x[0] = -1.0f; // ball_pos_x
    mask_flip_y[1] = -1.0f; // ball_pos_y
    mask_flip_x[3] = -1.0f; // ball_vel_x
    mask_flip_y[4] = -1.0f; // ball_vel_y

    for (int i = 0; i < 6; i++) {
        mask_flip_x[player_col_index(i, 0)] = -1.0f; // p{i}_pos_x
        mask_flip_y[player_col_index(i, 1)] = -1.0f; // p{i}_pos_y
        mask_flip_x[player_col_index(i, 3)] = -1.0f; // p{i}_vel_x
        mask_flip_y[player_col_index(i, 4)] = -1.0f; // p{i}_vel_y
        mask_flip_x[player_col_index(i, 6)] = -1.0f; // p{i}_rot_x
        mask_flip_y[player_col_index(i, 7)] = -1.0f; // p{i}_rot_y
        mask_flip_x[player_col_index(i, 9)] = -1.0f; // p{i}_up_x
        mask_flip_y[player_col_index(i, 10)] = -1.0f; // p{i}_up_y
        // Flipping x means negating angvel_[yz] and flipping y means negating angvel_[xz]
        mask_flip_y[player_col_index(i, 12)] = -1.0f; // p{i}_angvel_x
        mask_flip_x[player_col_index(i, 13)] = -1.0f; // p{i}_angvel_y
        mask_flip_x[player_col_index(i, 14)] = -1.0f; // p{i}_angvel_z
        mask_flip_y[player_col_index(i, 14)] = -1.0f; // p{i}_angvel_z
    }

    for (int i = 0; i < INPUT_DIM; i++) {
        mask_flip_xy[i] = mask_flip_x[i] * mask_flip_y[i];
    }
}

bool InferenceEngine::Initialize(const std::string& model_path_str) {
    try {
        InitializeInternal(model_path_str);

        // Run a test inference to make sure it works
        std::vector<float> test_input(INPUT_DIM, 0.0f);
        auto out_ptr = InferRaw(test_input);
        if (out_ptr.empty()) {
            LOG("Failed to make a test prediction with this model.");
            return false;
        }
        if (std::isnan(out_ptr[0] + out_ptr[1] + out_ptr[2])) {
            LOG("Got nan value from test prediction with this model.");
            return false;
        }
    }
    catch (const Ort::Exception& e) {
        LOG("Failed to load and test goal prediction model.");
        LOG(e.what());
        return false;
    }

    initialized = true;
    return true;
}

void InferenceEngine::Deinitialize() {
    initialized = false;
}

inline static void ApplyMask(const float* input, const float* mask, float* output, bool swap_teams = false) {
    for (size_t i = 0; i < INPUT_DIM; ++i) {
        output[i] = input[i] * mask[i];
    }
    if (swap_teams) {
        std::swap_ranges(
            output + NUM_BALL_COLS,
            output + NUM_BALL_COLS + NUM_PLAYER_COLS * 3,
            output + NUM_BALL_COLS + NUM_PLAYER_COLS * 3
        );
    }
}

inline static void SwapBoostX(float* data) {
    auto boosts = data + boost_index(0);
    std::swap(boosts[0], boosts[1]);
    std::swap(boosts[2], boosts[3]);
    std::swap(boosts[4], boosts[5]);
}

inline static void SwapBoostY(float* data) {
    auto boosts = data + boost_index(0);
    std::swap(boosts[0], boosts[4]);
    std::swap(boosts[1], boosts[5]);
}

inline static void SwapBoostXY(float* data) {
    auto boosts = data + boost_index(0);
    std::swap(boosts[0], boosts[5]);
    std::swap(boosts[1], boosts[4]);
    std::swap(boosts[2], boosts[3]);
}

inline static std::optional<double> GetKickoffTimeMs(const GameDataTracker& gameDataTracker, double currentTimeMs) {
    auto latestGoalTimeMs = gameDataTracker.GetMostRecentTimeMs<GoalEvent>(currentTimeMs);
    auto latestCountdownTimeMs = gameDataTracker.GetMostRecentTimeMs<KickoffEvent>(currentTimeMs);

    if (!latestGoalTimeMs && !latestCountdownTimeMs) {
        return std::nullopt;
    }
    else {
        return std::max(latestGoalTimeMs.value_or(-1), latestCountdownTimeMs.value_or(-1));
    }
}

inline static float GetRespawnTimerSec(double eventTimeMs, double currentTimeMs, double respawnPeriodMs) {
    auto respawnTimeMs = eventTimeMs + respawnPeriodMs;
    auto timeToRespawnMs = std::min(currentTimeMs - respawnTimeMs, 0.0);
    return static_cast<float>(timeToRespawnMs / 1000);
}

static std::string getSubarrayString(const std::vector<float>& data, size_t i, size_t L, int resolution = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(resolution);

    oss << "[";
    for (size_t j = 0; j < L; ++j) {
        oss << data[i + j];
        if (j % 3 == 2 && j < L - 1) {
            oss << " || ";
        } else if (j < L - 1) {
            oss << ", ";
        }
    }
    oss << "]";

    return oss.str();
}

std::optional<InferenceInput> InferenceEngine::GetInferenceInput(ServerWrapper server, const GameDataTracker& gameDataTracker, double currentTimeMs, bool logInputs) {
    if (!initialized || !session || !server || server.IsNull() || !server.GetbRoundActive()) {
        return std::nullopt;
    }

    std::vector<float> inputs(INPUT_DIM, 0.0f);

    auto ball = server.GetBall();
    // GetExplosionTime() only set during PostGoalScored time (not to be confused with post-goal ReplayPlayback)
    if (ball.IsNull() || ball.GetExplosionTime() > 0) {
        return std::nullopt;
    }

    auto ballPos = ball.GetCurrentRBState().Location;
    auto ballVel = ball.GetCurrentRBState().LinearVelocity;
    // Negate x-values to make them match a normal 3d space for viewers, though doesn't actually matter for inference since we augment anyway
    inputs[0] = -ballPos.X;
    inputs[1] = ballPos.Y;
    inputs[2] = ballPos.Z;
    inputs[3] = -ballVel.X;
    inputs[4] = ballVel.Y;
    inputs[5] = ballVel.Z;
    if (logInputs) {
        LOG("---- Model inputs at game time {}", currentTimeMs);
        LOG("BALL: {}", getSubarrayString(inputs, 0, 6));
    }

    // Load in stored big boost and demo data to infer respawn timers.
    auto mostRecentKickoffTimeMs = GetKickoffTimeMs(gameDataTracker, currentTimeMs);

    auto boostPickupMinTimeMs = currentTimeMs - (BIG_BOOST_RESPAWN_PERIOD_MS + EVENT_LOOKUP_GRACE_MS);
    boostPickupMinTimeMs = std::max(boostPickupMinTimeMs, mostRecentKickoffTimeMs.value_or(boostPickupMinTimeMs));
    auto boostPickupEvents = gameDataTracker.GetRangeInclusive<BigBoostPickupEvent>(boostPickupMinTimeMs, currentTimeMs);

    auto demolitionMinTimeMs = currentTimeMs - (PLAYER_RESPAWN_PERIOD_MS + EVENT_LOOKUP_GRACE_MS);
    demolitionMinTimeMs = std::max(demolitionMinTimeMs, mostRecentKickoffTimeMs.value_or(demolitionMinTimeMs));
    auto demolitionEvents = gameDataTracker.GetRangeInclusive<DemolitionEvent>(demolitionMinTimeMs, currentTimeMs);

    auto PRIs = server.GetPRIs();
    int num_team0_found = 0;
    int num_team1_found = 0;
    for (int i = 0; i < PRIs.Count(); ++i) {
        PriWrapper pri = PRIs.Get(i);
        if (pri.IsNull() || pri.IsSpectator() || pri.GetTeamNum() > 1) {
            continue;
        }

        int p_index = 0;
        if (pri.GetTeamNum() == 0) {
            p_index = num_team0_found;
            num_team0_found += 1;
        }
        else {
            p_index = 3 + num_team1_found;
            num_team1_found += 1;
        }

        auto car = pri.GetCar();
        if (car.IsNull() || car.GetbHidden()) { // Demolished. Former case can occur when seeking forward in a replay, but usually it's latter case
            // Set everything but respawn_timer to nan
            std::fill(inputs.begin() + player_col_index(p_index, 0), inputs.begin() + player_col_index(p_index, NUM_PLAYER_COLS - 1), NAN);

            // Hopefully infer respawn timer from our demo data
            auto id = GetId(pri);
            inputs[player_col_index(p_index, 16)] = -1.0f; // This is our default in case we can't find the demo event :shrug:
            for (auto const& [timeMs, demolition] : demolitionEvents | std::views::reverse) {
                if (id == demolition.victim_pri_id) {
                    inputs[player_col_index(p_index, 16)] = GetRespawnTimerSec(timeMs, currentTimeMs, PLAYER_RESPAWN_PERIOD_MS);
                    break;
                }
            }
        }
        else {
            auto carPos = car.GetCurrentRBState().Location;
            auto carVel = car.GetCurrentRBState().LinearVelocity;
            auto [carRot, carUp] = RotatorToRotAndUpVectors(car.GetRotation());
            auto carAngVel = car.GetCurrentRBState().AngularVelocity;

            // Flip over x-axis to match a normal 3d space for viewers, though doesn't really matter here
            inputs[player_col_index(p_index, 0)] = -carPos.X;
            inputs[player_col_index(p_index, 1)] = carPos.Y;
            inputs[player_col_index(p_index, 2)] = carPos.Z;
            inputs[player_col_index(p_index, 3)] = -carVel.X;
            inputs[player_col_index(p_index, 4)] = carVel.Y;
            inputs[player_col_index(p_index, 5)] = carVel.Z;
            inputs[player_col_index(p_index, 6)] = -carRot.X;
            inputs[player_col_index(p_index, 7)] = carRot.Y;
            inputs[player_col_index(p_index, 8)] = carRot.Z;
            inputs[player_col_index(p_index, 9)] = -carUp.X;
            inputs[player_col_index(p_index, 10)] = carUp.Y;
            inputs[player_col_index(p_index, 11)] = carUp.Z;
            // Negate angvel_[yz] when flipping over x-axis
            // Convert angvel from deg / sec to rad / sec which the model expects
            inputs[player_col_index(p_index, 12)] = carAngVel.X * DEG_TO_RAD;
            inputs[player_col_index(p_index, 13)] = -carAngVel.Y * DEG_TO_RAD;
            inputs[player_col_index(p_index, 14)] = -carAngVel.Z * DEG_TO_RAD;
            inputs[player_col_index(p_index, 15)] = !car.GetBoostComponent().IsNull()
                ? 100 * car.GetBoostComponent().GetPercentBoostFull()
                : 0.0f;
            inputs[player_col_index(p_index, 16)] = NAN; // respawn_timer
        }

        if (logInputs) {
            LOG("P{} ({}): {}", p_index, pri.GetPlayerName().ToString(), getSubarrayString(inputs, player_col_index(p_index, 0), NUM_PLAYER_COLS));
        }
    }

    if (num_team0_found != 3 || num_team1_found != 3) {
        return std::nullopt;
    }

    // Hopefully infer boost respawn timers from pickup events
    std::fill(inputs.begin() + boost_index(0), inputs.begin() + boost_index(6), NAN); // Set to NAN as default which indicates boost is live
    for (auto const& [timeMs, boostPickup] : boostPickupEvents | std::views::reverse) {
        auto index = boost_index(boostPickup.boost_index);
        if (std::isnan(inputs[index])) {
            inputs[index] = GetRespawnTimerSec(timeMs, currentTimeMs, BIG_BOOST_RESPAWN_PERIOD_MS);
        }
    }

    if (logInputs) {
        LOG("BOOSTS: {}", getSubarrayString(inputs, boost_index(0), 6));
    }

    PredictionReliability reliability = server.GetSecondsRemaining() > NEAR_ZERO_SECONDS_UNRELIABLE_THRESHOLD_SEC || server.GetbOverTime()
        ? RELIABLE
        : UNRELIABLE_NEAR_ZERO_SECONDS;
    // No more UNRELIABLE_MISSING_PAST_DATA checks anymore since it's uncommon, only induces minor changes, and is kinda confusing UX.

    return InferenceInput{ inputs, reliability };
}

// Run the model to make our predictions, optionally augmenting the data and averaging the results.
std::optional<Prediction> InferenceEngine::Predict(InferenceInput input, Augmentation augmentation) {
    auto augmentationNum = (int)augmentation;
    std::vector<float> batch_input(augmentationNum * INPUT_DIM, 0.0f);
    float* batch_input_ptr = batch_input.data();
    const float* input_ptr = input.inputs.data();

    // Depending on augmentation, construct batches up to:
    // 1. Identity  2. flip_xy  3. flip_x  4. flip_y
    switch (augmentation) {
    case AUGMENT_4X:
        ApplyMask(input_ptr, mask_flip_y.data(), batch_input_ptr + 3 * INPUT_DIM, true);
        SwapBoostY(batch_input_ptr + 3 * INPUT_DIM);
        ApplyMask(input_ptr, mask_flip_x.data(), batch_input_ptr + 2 * INPUT_DIM);
        SwapBoostX(batch_input_ptr + 2 * INPUT_DIM);
        [[fallthrough]];
    case AUGMENT_2X:
        ApplyMask(input_ptr, mask_flip_xy.data(), batch_input_ptr + 1 * INPUT_DIM, true);
        SwapBoostXY(batch_input_ptr + 1 * INPUT_DIM);
        [[fallthrough]];
    case NO_AUGMENT:
        std::copy_n(input_ptr, INPUT_DIM, batch_input_ptr);
    }

    auto startTimeMs = GetCurrentEpochTimeMs();
    const std::vector<float> batch_output = InferRaw(batch_input);
    auto endTimeMs = GetCurrentEpochTimeMs();
    if (batch_output.empty()) {
        return std::nullopt;
    }

    // The output is N sets of three, each [prob_blue, prob_orange, prob_neither].
    // Average results from the N inferences, swapping teams on outputs from y flips
    float prob_blue = 0.0, prob_orange = 0.0;
    switch (augmentation) {
    case AUGMENT_4X:
        prob_blue += batch_output[10] + batch_output[6];
        prob_orange += batch_output[9] + batch_output[7];
        [[fallthrough]];
    case AUGMENT_2X:
        prob_blue += batch_output[4];
        prob_orange += batch_output[3];
        [[fallthrough]];
    case NO_AUGMENT:
        prob_blue += batch_output[0];
        prob_orange += batch_output[1];
    }
    prob_blue /= augmentationNum;
    prob_orange /= augmentationNum;

    return Prediction(prob_blue, prob_orange, input.reliability, augmentation, endTimeMs - startTimeMs);
}

const std::vector<float> InferenceEngine::InferRaw(std::vector<float> input) {
    if (input.size() % INPUT_DIM != 0) {
        return {};
    }
    int num_batches = static_cast<int>(input.size() / INPUT_DIM);
    std::vector<int64_t> batch_input_dims = { num_batches, INPUT_DIM };

    try {
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            cpu_memory_info,
            input.data(),
            input.size(),
            batch_input_dims.data(),
            batch_input_dims.size()
        );

        auto output_tensors = session->Run(
            Ort::RunOptions { nullptr },
            input_node_names_ptr.data(),
            &input_tensor,
            1,
            output_node_names_ptr.data(),
            1
        );

        auto out_ptr = output_tensors[0].GetTensorData<float>();
        auto out_vector = std::vector<float>(out_ptr, out_ptr + num_batches * OUTPUT_DIM);

        // Ensure outputs are not nan / infty and in correct range
        for (auto val : out_vector) {
            if (!(val >= 0.0f && val <= 1.0f)) {
                LOG("INVALID MODEL OUTPUT: {}", val);
                return {};
            }
        }

        return out_vector;
    }
    catch (const Ort::Exception& e) {
        LOG("Inference error!");
        LOG(e.what());
        return {};
    }
}

std::optional<int> InferenceEngine::GetBigBoostIndex(Vector location) {
    // Unlike above, we do *not* negate x-values here to make them match a normal x-y space
    // we just leave them in pure game coordinates, and assume the location is as well.
    static const Vector BIG_BOOST_LOCATIONS[6] = {
        {  3072.0f, -4096.0f, 72.0f},
        { -3072.0f, -4096.0f, 72.0f},
        {  3584.0f,     0.0f, 72.0f},
        { -3584.0f,     0.0f, 72.0f},
        {  3072.0f,  4096.0f, 72.0f},
        { -3072.0f,  4096.0f, 72.0f},
    };
    static const float EPSILON = 1.0f;

    for (int i = 0; i < 6; i++) {
        if ((BIG_BOOST_LOCATIONS[i] - location).magnitude() < EPSILON) {
            return i;
        }
    }

    return std::nullopt;
}
