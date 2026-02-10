#pragma once
#include <string>

enum GameType {
    NONE,
    REPLAY,
    ONLINE,
};

struct GameKey {
    GameType type;
    std::string guid;

    bool IsActive() const {
        return type != NONE;
    }

    auto operator<=>(const GameKey&) const = default;
};

const GameKey GAME_KEY_NONE = { NONE, "" };

struct SecondEvent {
    int second;
    bool overtime;

    auto operator<=>(const SecondEvent&) const = default;
};

struct BallHitEvent {
    bool orange; // else blue

    auto operator<=>(const BallHitEvent&) const = default;
};

struct DemolitionEvent {
    bool victim_orange; // else blue
    std::string victim_pri_id;

    auto operator<=>(const DemolitionEvent&) const = default;
};

struct GoalEvent {
    bool orange_scored; // else blue

    auto operator<=>(const GoalEvent&) const = default;
};

struct KickoffEvent {
    auto operator<=>(const KickoffEvent&) const = default;
};

struct BigBoostPickupEvent {
    int boost_index;

    auto operator<=>(const BigBoostPickupEvent&) const = default;
};

// The model gives more accurate results with test-time augmentation, basically flipping the input data
// over x- and/or y-axes to get multiple inputs and averaging the outputs.
// Technically we could augment up to 144X by permuting the player order, but that's extreme, and our model
// is invariant to those anyway.
enum Augmentation {
    // A bit hacky, but we do depend on these int values matching correctly.
    NO_AUGMENT = 1, // Just predict on the raw inputs
    AUGMENT_2X = 2, // Add flip_xy
    AUGMENT_4X = 4, // Add flip_x, flip_y, flip_xy
};

enum PredictionReliability {
    RELIABLE,
    // Without "continuous" data past data we can't reliably infer boost / player respawn timers
    // NOTE: No longer used, it behaved inconsistently depending on replay handling and ultimately has
    // minor impact on predictions and IMO not worth the UX complexity.
    //UNRELIABLE_MISSING_PAST_DATA,
    UNRELIABLE_NEAR_ZERO_SECONDS, // We don't include game time as prediction input, so we can't reliably predict divergent zero-second behavior
};

struct Prediction {
    float prob_blue;
    float prob_orange;
    float prob_delta; // blue - orange
    // Whether the prediction can be trusted, e.g. if respawn timers cannot be inferred or nearing zero seconds
    // which the model was not trained to account for.
    PredictionReliability reliability;
    Augmentation augmentation;
    double prediction_time_ms;

    Prediction() = default;
    Prediction(float prob_blue, float prob_orange, PredictionReliability reliability, Augmentation augmentation, double prediction_time_ms) {
        this->prob_blue = prob_blue;
        this->prob_orange = prob_orange;
        this->prob_delta = prob_blue - prob_orange;
        this->reliability = reliability;
        this->augmentation = augmentation;
        this->prediction_time_ms = prediction_time_ms;
    }

    auto operator<=>(const Prediction&) const = default;
};