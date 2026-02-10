# Rocket League Goal Predictor

![Screenshot](https://github.com/user-attachments/assets/107c7af5-59cb-4955-bedf-b42e1cb8f8bb)

**Goal Predictor** is a [BakkesMod](https://bakkesmod.com/) plugin for Rocket League that runs a machine learning model to predict the probability of each team scoring within the next 10 seconds. The predictions are rendered as a real-time scrolling graph overlay, allowing you to visualize which team has the advantage as it shifts. We use a Transformer-based model trained on **RLCS data**.

This plugin currently only works for **3v3 matches** when **spectating** online games or viewing **replays**.

## Model

Our machine learning model has about 1.8 million parameters and utilizes a custom **Transformer architecture**, similar to the technology powering LLMs. Each prediction is made from one snapshot of in-game data (no past frames). The model represents the game state as a collection of **15 entities**:
*   **1 Ball** (position, velocity)
*   **6 Players** (position, velocity, forward direction, up direction, angular velocity, boost, respawn timer)
*   **6 Big boost orbs** (fixed position, respawn timer)
*   **2 Goals** (fixed position)

and uses a custom attention mechanism with geometric biases to weigh the importance of every entity-to-entity interaction, then aggregates this information into a final probability distribution prediction.

The model does *not* have access to **current score** or **current time** information. This is to focus purely on current game state and avoid a bias of which team is currently winning, but it also means the model has **no understanding of realistic zero-second behavior** where 1) the 10 second time horizon is cut off and 2) each team's gameplay tactics might diverge from normal based on the current score or the need to keep the ball up.

The model also does not have access to whether players have their **flip available**.  This would be very useful information, but unfortunately I don't believe it's possible to accurately determine from replay files nor from the in-game data available to BakkesMod.  Instead the model tries to infer based on other context.

### Training
The model is trained on in-game data from Rocket League Championship Series (RLCS) matches, specifically online matches from North America and Europe in Seasons 2022-23, 2024, and 2025.

## Open-source resources

This project is entirely open-source, from the raw replay files and parsing, to the ML model, to this BakkesMod plugin.

1.  **RLCS replay files:** A collection of Europe, North America, and LAN replay files from RLCS [Season X](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-season-x-replays), [21-2022](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-season-2021-22-replays), [2022-23](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-season-2022-23-replays), [2024](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-season-2024-replays), and [2025](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-season-2025-replays).
2.  **[Replay parsing library](https://www.kaggle.com/datasets/dster/carball-fork):** Custom fork of the abandoned [carball](https://github.com/SaltieRL/carball) project to parse replay files, with various bugfixes and improvements.
3.  **[Timeseries parsing code](https://www.kaggle.com/code/dster/rocket-league-rlcs-timeseries-parsing):** Parses replays and cleans / filters bad data to construct the Timeseries Dataset.
4.  **[Timeseries dataset](https://www.kaggle.com/datasets/dster/rocket-league-rlcs-timeseries):** The parsed, cleaned in-game timeseries data from the RLCS replay files.
5.  **[Kaggle Competition](https://www.kaggle.com/competitions/rocket-league-rlcs-goal-prediction-2025):** Testbed to train a model and verify its accuracy on the test dataset.
6.  **[Model code](https://kaggle.com/code/dster/rocket-league-goal-predictor-2025-model):** ML model definition, training, and export pipelines.
7.  **[BakkesMod plugin](https://github.com/dster2/rocket-league-goal-predictor):** This plugin which runs the model on in-game data in real-time within Rocket League itself.

## Custom models

You can use your own ML model instead of the included one. The plugin just reads the ONNX model file located at `bakkesmod/data/goal_predictor_model_3v3.onnx` which you can replace with your own, as long as you match the expected input / output formats.

The model takes input tensor of shape `(Batch, 114)` and returns outputs of shape `(Batch, 3)`, all `float32` type, whose data formats correspond exactly to the test set inputs and outputs of the [Kaggle Competition](https://www.kaggle.com/competitions/rocket-league-rlcs-goal-prediction-2025) (besides the `id` column).  Note: just like how the Competition's test inputs have some `null` entries (e.g. when a player is demoed) the plugin sometimes send `nan` values in its input tensors which your model must handle gracefully.

## Installation

1. Navigate to the [Releases](https://github.com/dster2/rocket-league-goal-predictor/releases) page and download the latest `zip` file.
2. Extract the contents.
3. Copy the contents of the extracted `bakkesmod` folder to your system’s BakkesMod directory (often found at `C:\Users\[you]\AppData\Roaming\bakkesmod\bakkesmod`).
4. Launch Rocket League, open the BakkesMod menu (`F2`), go to `Plugins` tab, open `Plugin Manager`, and enable `Goal Predictor`.

## Build

This project uses `vcpkg` to build the `onnxruntime` dependency into the output `GoalPredictor.dll` (see [BakkesMod documentation](https://bakkesplugins.com/wiki/bakkesmod-sdk/plugin-tutorial/3rdparty-dependencies)). You must use the included `triplets/x64-windows-ort-static.cmake` triplet file when building, which should be already configured in the VS Project settings.

I experienced `ninja` "Access Violation" errors when building on my machine, which occurred randomly on various dependencies upstream of `onnxruntime`.  I think this was due to race conditions or OOM issues caused by running with too much concurrency in the build job. When I added a **Windows Environment Variable** `VCPKG_MAX_CONCURRENCY=4`, the build succeeded; by default it was originally using concurrency 29.

You could also just disable the `vcpkg` integration, download the [`onnxruntime` libraries](https://github.com/microsoft/onnxruntime/releases) (we need `onnxruntime.dll` and `onnxruntime_providers_shared.dll`), link them to the VS Project to get it to build, and copy those `dll`s into the directory where `RocketLeague.exe` lives (often `C:\Program Files (x86)\Steam\steamapps\common\rocketleague\Binaries\Win64`) so the game can find them.

I used Visual Studio 2022 (v143) with C++ 20.

### Data files

If you build locally make sure to copy the contents of `Release/bakkesmod/data` into your installed `bakkesmod/data` folder to get the model and font data files required by the plugin.
