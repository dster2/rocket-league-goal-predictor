#include "pch.h"
#include "bakkesmod/wrappers/GuiManagerWrapper.h"
#include "GoalPredictor.h"
#include "utils.h"

inline static std::string to_utf8(ImWchar c) {
	std::string out;
	if (c < 0x80) {
		out.push_back(static_cast<char>(c));
	}
	else if (c < 0x800) {
		out.push_back(static_cast<char>(0xc0 | (c >> 6)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
	}
	else {
		out.push_back(static_cast<char>(0xe0 | (c >> 12)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
	}
	return out;
}

inline static ImU32 with_alpha(ImU32 color, unsigned short alpha) {
	return (color & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}

ImFont* emojiFont;
static ImVector<ImWchar> emojiFontRanges;

const std::string EMOJI_FONT_NAME = "emoji-font";
const std::string EMOJI_FONT_FILE_NAME = "NotoEmoji-Light.ttf";
const int EMOJI_FONT_SIZE = 32;
const ImWchar BALL_EMOJI_CHAR = 0x26BD;
const ImWchar DEMO_EMOJI_CHAR = 0x2620;
const ImWchar GOAL_EMOJI_CHAR = 0x2795;
const std::string BALL_EMOJI_UTF8 = to_utf8(BALL_EMOJI_CHAR);
const std::string DEMO_EMOJI_UTF8 = to_utf8(DEMO_EMOJI_CHAR);
const std::string GOAL_EMOJI_UTF8 = to_utf8(GOAL_EMOJI_CHAR);

const double MAX_PREDICTION_LINE_TIME_GAP_MS = 100;
const double MAX_TOOLTIP_MOUSE_DIST_MS = 100;

const int GAUGE_WIDTH = 48;
const int GAUGE_PADDING = 6;
const int GAUGE_BASE_ALPHA = 50;

const int EMOJI_ZONE_HEIGHT = 48;
const int EMOJI_ORANGE_OFFSET = 17;

const float SIGMOID_SCALE = 8.0f;

const ImU32 COL_BLUE = IM_COL32(60, 120, 255, 255);
const ImU32 COL_ORANGE = IM_COL32(255, 150, 60, 255);
const ImU32 COL_WHITE = IM_COL32(255, 255, 255, 255);
const ImU32 COL_BLACK = IM_COL32(0, 0, 0, 255);
const ImU32 COL_GRAY = IM_COL32(168, 168, 168, 255);
const ImU32 COL_YELLOW = IM_COL32(255, 255, 0, 255);
const ImVec4 COL_YELLOW_VEC4 = ImColor(IM_COL32(255, 255, 0, 255));

const ImU32 COL_BG_BLUE = with_alpha(COL_BLUE, 50);
const ImU32 COL_BG_ORANGE = with_alpha(COL_ORANGE, 50);
const ImU32 COL_BORDER = IM_COL32(100, 100, 100, 255);
const ImU32 COL_GRID = with_alpha(COL_WHITE, 64);
const ImU32 COL_GRID_BOLD = with_alpha(COL_WHITE, 150);
const ImU32 COL_TOOLTIP_LINE = with_alpha(COL_WHITE, 128);
const ImU32 COL_TEXT = IM_COL32(200, 200, 200, 255);
const ImU32 COL_GAUGE_BASE = IM_COL32(0, 0, 0, 255);

inline static ImU32 GetTeamColor(bool orange) {
	return orange ? COL_ORANGE : COL_BLUE;
}

float currentGaugeDelta = 0.0f;
double averagePredictionTimeMs = 0;

struct GraphContext {
	ImVec2 pMin; // Top left
	ImVec2 pMax; // Bottom right
	ImVec2 size;
	double tMin;
	double tMax;
	float yMin = -1.0f;
	float yMax = 1.0f;

	float ToScreenX(double t) const {
		double normalized = (t - tMin) / (tMax - tMin);
		return static_cast<float>(pMin.x + normalized * size.x);
	}

	float ToScreenYRaw(float v) const {
		float normalized = (v - yMin) / (yMax - yMin);
		return pMax.y - normalized * size.y; // Invert Y (0 is top)
	}

	float ScaleValue(float v) const {
		static const float norm_factor = 1.0f / asinhf(SIGMOID_SCALE);
		return asinhf(SIGMOID_SCALE * v) * norm_factor;
	}

	float ToScreenYScaled(float v) const {
		return ToScreenYRaw(ScaleValue(v));
	}
};

void GoalPredictor::LoadRenderer() {
	auto gui = gameWrapper->GetGUIManager();

	emojiFontRanges.clear();
	ImFontGlyphRangesBuilder rangesBuilder;
	rangesBuilder.AddChar(BALL_EMOJI_CHAR);
	rangesBuilder.AddChar(DEMO_EMOJI_CHAR);
	rangesBuilder.AddChar(GOAL_EMOJI_CHAR);
	rangesBuilder.BuildRanges(&emojiFontRanges);

	auto [res, font] = gui.LoadFont(EMOJI_FONT_NAME, EMOJI_FONT_FILE_NAME, EMOJI_FONT_SIZE, nullptr, emojiFontRanges.Data);

	if (res == 0) {
		LOG("Failed to load the emoji font!");
	}
	else if (res == 1) {
		LOG("The emoji font is queued for loading.");
	}
	else if (res == 2 && font) {
		emojiFont = font;
		LOG("Emoji font loaded!");
	}
}

static void DrawGrid(ImDrawList* dl, const GraphContext& ctx) {
	// Background
	float midY = ctx.pMin.y + ctx.size.y / 2;
	dl->AddRectFilled(ctx.pMin, ImVec2(ctx.pMax.x, midY), with_alpha(COL_BLUE, 50));
	dl->AddRectFilled(ImVec2(ctx.pMin.x, midY), ctx.pMax, COL_BG_ORANGE);
	dl->AddRect(ctx.pMin, ctx.pMax, COL_BORDER);

	// Horizontal percentage grid lines
	static const int gridLines[] = { 5, 10, 25, 50, 75 };
	static int numLines = std::size(gridLines);
	for (int i = -numLines + 1; i < numLines; i++) {
		int gridLine = i == 0 ? 0 : gridLines[abs(i)];
		int gridLineSigned = gridLine * (i < 0 ? -1 : 1);

		float y_screen = ctx.ToScreenYScaled(gridLineSigned / 100.0f);
		auto percent_str = std::format("{}%", gridLine);

		ImU32 color = i == 0 ? COL_GRID_BOLD : COL_GRID;
		float thickness = i == 0 ? 2.0f : 1.0f;

		dl->AddLine(ImVec2(ctx.pMin.x, y_screen), ImVec2(ctx.pMax.x, y_screen), color, thickness);

		ImVec2 textSize = ImGui::CalcTextSize(percent_str.c_str());
		dl->AddText(ImVec2(ctx.pMin.x + 3, y_screen - textSize.y - 1), COL_TEXT, percent_str.c_str());
	}
}

static void DrawEventLines(ImDrawList* dl, const GraphContext& ctx, const GameDataTracker& gameDataTracker) {
	auto secondEvents = gameDataTracker.GetRangeInclusive<SecondEvent>(ctx.tMin, ctx.tMax);
	for (auto const& [timeMs, secondEvent] : secondEvents) {
		float x = ctx.ToScreenX(timeMs);
		dl->AddLine(ImVec2(x, ctx.pMin.y), ImVec2(x, ctx.pMax.y), COL_GRID, 1.0f);

		int minutes = secondEvent.second / 60;
		int seconds = secondEvent.second % 60;
		auto time_str = std::format("{}:{:02}", minutes, seconds);
		ImVec2 textSize = ImGui::CalcTextSize(time_str.c_str());
		dl->AddText(ImVec2(x + 4, ctx.pMax.y - textSize.y - 2), COL_TEXT, time_str.c_str());
	}

	auto ballHitEvents = gameDataTracker.GetRangeInclusive<BallHitEvent>(ctx.tMin, ctx.tMax);
	for (auto const& [timeMs, ballHitEvent] : ballHitEvents) {
		float x = ctx.ToScreenX(timeMs);
		dl->AddLine(ImVec2(x, ctx.pMin.y), ImVec2(x, ctx.pMax.y), GetTeamColor(ballHitEvent.orange), 1.0f);
	}

	// Dashed line for demos
	auto demolitionEvents = gameDataTracker.GetRangeInclusive<DemolitionEvent>(ctx.tMin, ctx.tMax);
	const int numLines = 7;
	const float gapLineRatio = 0.5;
	const float lineLength = 2.0f / (numLines + gapLineRatio * (numLines - 1));
	const float gapLength = gapLineRatio * lineLength;
	const float bothLength = lineLength + gapLength;
	for (auto const& [timeMs, demolitionEvent] : demolitionEvents) {
		float x = ctx.ToScreenX(timeMs);
		for (int i = 0; i < numLines; i++) {
			float y0 = ctx.ToScreenYRaw(-1.0f + bothLength * i);
			float y1 = ctx.ToScreenYRaw(-1.0f + bothLength * i + lineLength);
			dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), GetTeamColor(!demolitionEvent.victim_orange), 1.0f);
		}
	}

	auto goalEvents = gameDataTracker.GetRangeInclusive<GoalEvent>(ctx.tMin, ctx.tMax);
	for (auto const& [timeMs, goalEvent] : goalEvents) {
		float x = ctx.ToScreenX(timeMs);
		dl->AddLine(ImVec2(x, ctx.pMin.y), ImVec2(x, ctx.pMax.y), GetTeamColor(goalEvent.orange_scored), 3.0f);
	}
}

static void DrawPredictions(ImDrawList* dl, const GraphContext& ctx, const GameDataTracker& gameDataTracker) {
	auto predictions = gameDataTracker.GetRangeInclusive<Prediction>(ctx.tMin, ctx.tMax);
	auto prevIt = predictions.begin();
	if (prevIt == predictions.end()) {
		return;
	}

	for (auto nextIt = std::next(prevIt); nextIt != predictions.end(); ++prevIt, ++nextIt) {
		auto& [t1, p1] = *prevIt;
		auto& [t2, p2] = *nextIt;

		if (t2 - t1 >= MAX_PREDICTION_LINE_TIME_GAP_MS) {
			continue;
		}

		float x1 = ctx.ToScreenX(t1);
		float x2 = ctx.ToScreenX(t2);

		dl->AddLine(
			ImVec2(x1, ctx.ToScreenYScaled(p1.prob_blue)),
			ImVec2(x2, ctx.ToScreenYScaled(p2.prob_blue)),
			COL_BLUE, 1.0f);

		dl->AddLine(
			ImVec2(x1, ctx.ToScreenYScaled(-p1.prob_orange)),
			ImVec2(x2, ctx.ToScreenYScaled(-p2.prob_orange)),
			COL_ORANGE, 1.0f);

		dl->AddLine(
			ImVec2(x1, ctx.ToScreenYScaled(p1.prob_delta)),
			ImVec2(x2, ctx.ToScreenYScaled(p2.prob_delta)),
			p2.reliability == RELIABLE ? COL_WHITE : COL_YELLOW, 4.0f);
	}
}

static void DrawTooltip(Prediction prediction) {
	ImGui::BeginTooltip();
	ImGui::Text("Blue: %.1f%%", prediction.prob_blue * 100.0f);
	ImGui::Text("Orange: %.1f%%", prediction.prob_orange * 100.0f);
	ImGui::Text("Diff: %.1f%%", prediction.prob_delta * 100.0f);

	if (prediction.reliability == UNRELIABLE_NEAR_ZERO_SECONDS) {
		ImGui::TextColored(COL_YELLOW_VEC4, "Predictions do not account");
		ImGui::TextColored(COL_YELLOW_VEC4, "for zero-second behavior.");
	}

	ImGui::EndTooltip();
}

static void TryDrawGraphTooltip(ImDrawList* dl, const GraphContext& ctx, const GameDataTracker& gameDataTracker) {
	if (!ImGui::IsMouseHoveringRect(ctx.pMin, ctx.pMax)) {
		return;
	}

	// First try to find closest prediction to mouse position based on t-axis.
	ImVec2 mousePos = ImGui::GetMousePos();
	float mouseRatioX = (mousePos.x - ctx.pMin.x) / ctx.size.x;
	double hoverTimeMs = ctx.tMin + mouseRatioX * (ctx.tMax - ctx.tMin);

	auto closestPredOptional = gameDataTracker.GetClosest<Prediction>(hoverTimeMs);
	if (!closestPredOptional) {
		return;
	}
	auto [predictionTimeMs, prediction] = closestPredOptional.value();
	if (std::abs(predictionTimeMs - hoverTimeMs) > MAX_TOOLTIP_MOUSE_DIST_MS) {
		return;
	}

	float x = ctx.ToScreenX(predictionTimeMs);

	dl->AddLine(ImVec2(x, ctx.pMin.y), ImVec2(x, ctx.pMax.y), COL_TOOLTIP_LINE, 1.0f);

	dl->AddCircleFilled(ImVec2(x, ctx.ToScreenYScaled(prediction.prob_blue)), 2.0f, COL_BLUE);
	dl->AddCircleFilled(ImVec2(x, ctx.ToScreenYScaled(-prediction.prob_orange)), 2.0f, COL_ORANGE);
	dl->AddCircleFilled(ImVec2(x, ctx.ToScreenYScaled(prediction.prob_delta)), 4.0f, prediction.reliability == RELIABLE ? COL_WHITE : COL_YELLOW);

	DrawTooltip(prediction);
}

static void DrawGauge(ImDrawList* dl, const GraphContext& ctx, const GameDataTracker& gameDataTracker, double currentTimeMs) {
	// Get the most recent prediction
	auto mostRecentPrediction = gameDataTracker.GetMostRecent<Prediction>(currentTimeMs);
	if (!mostRecentPrediction) {
		return;
	}
	auto [timeMs, prediction] = mostRecentPrediction.value();
	if (currentTimeMs - timeMs > MAX_PREDICTION_LINE_TIME_GAP_MS) {
		currentGaugeDelta = 0.0f;
		return;
	}

	float delta = prediction.prob_delta;

	const float smoothSpeed = 10.0f;
	float diff = delta - currentGaugeDelta;
	ImGuiIO& io = ImGui::GetIO();

	currentGaugeDelta += diff * smoothSpeed * io.DeltaTime;

	delta = currentGaugeDelta;

	auto y_zero = ctx.ToScreenYScaled(0.0f);
	auto y_delta = ctx.ToScreenYScaled(delta);

	// Draw the gradient bar
	if (delta > 0) {
		// Bar goes UP from y_zero to y_delta
		auto base_color = with_alpha(COL_BLUE, GAUGE_BASE_ALPHA);
		dl->AddRectFilledMultiColor(
			ImVec2(ctx.pMin.x, y_delta),
			ImVec2(ctx.pMax.x, y_zero),
			COL_BLUE, COL_BLUE, base_color, base_color
		);
	}
	else {
		// Bar goes DOWN from y_zero to y_delta
		auto base_color = with_alpha(COL_ORANGE, GAUGE_BASE_ALPHA);
		dl->AddRectFilledMultiColor(
			ImVec2(ctx.pMin.x, y_zero),
			ImVec2(ctx.pMax.x, y_delta),
			base_color, base_color, COL_ORANGE, COL_ORANGE
		);
	}

	dl->AddLine(ImVec2(ctx.pMin.x, y_zero), ImVec2(ctx.pMax.x, y_zero), COL_GRID_BOLD, 2.0f);
	dl->AddLine(ImVec2(ctx.pMin.x, y_delta), ImVec2(ctx.pMax.x, y_delta), prediction.reliability == RELIABLE ? COL_WHITE : COL_YELLOW, 4.0f);

	int pct = static_cast<int>(round(std::abs(delta) * 100));
	auto percent_str = std::format("{}%", pct);

	// Center text X, Position Y slightly above/below line to not cover it
	ImVec2 text_size = ImGui::CalcTextSize(percent_str.c_str());
	float text_x = ctx.pMin.x + (ctx.size.x - text_size.x) / 2;
	float text_y = y_delta - text_size.y - 2;

	// If too high (near top), move text below line
	if (text_y < ctx.pMin.y) {
		text_y = y_delta + 4;
	}

	dl->AddText(ImVec2(text_x + 1, text_y + 1), COL_BLACK, percent_str.c_str()); // Shadow
	dl->AddText(ImVec2(text_x, text_y), COL_WHITE, percent_str.c_str());

	if (ImGui::IsMouseHoveringRect(ctx.pMin, ctx.pMax)) {
		DrawTooltip(prediction);
	}
}

static inline void DrawEmoji(ImDrawList* dl, const GraphContext& ctx, double timeMs, bool orange, std::string emoji) {
	dl->AddText(ImVec2(ctx.ToScreenX(timeMs) - EMOJI_FONT_SIZE / 2, ctx.pMin.y - 2 + EMOJI_ORANGE_OFFSET * orange), GetTeamColor(orange), emoji.c_str());
}

static void DrawEmojiBar(ImDrawList* dl, const GraphContext& ctx, const GameDataTracker& gameDataTracker, const bool logPredictionTime) {
	if (!emojiFont) {
		return;
	}

	dl->PushClipRect(ctx.pMin, ctx.pMax, true);

	auto ballHitEvents = gameDataTracker.GetRangeInclusive<BallHitEvent>(ctx.tMin, ctx.tMax);
	auto demolitionEvents = gameDataTracker.GetRangeInclusive<DemolitionEvent>(ctx.tMin, ctx.tMax);
	auto goalEvents = gameDataTracker.GetRangeInclusive<GoalEvent>(ctx.tMin, ctx.tMax);

	ImGui::PushFont(emojiFont);

	for (auto const& [timeMs, ballHitEvent] : ballHitEvents) {
		DrawEmoji(dl, ctx, timeMs, ballHitEvent.orange, BALL_EMOJI_UTF8);
	}

	for (auto const& [timeMs, demolitionEvent] : demolitionEvents) {
		DrawEmoji(dl, ctx, timeMs, !demolitionEvent.victim_orange, DEMO_EMOJI_UTF8);
	}

	for (auto const& [timeMs, goalEvent] : goalEvents) {
		DrawEmoji(dl, ctx, timeMs, goalEvent.orange_scored, GOAL_EMOJI_UTF8);
	}

	ImGui::PopFont();
	dl->PopClipRect();
}

void GoalPredictor::RenderWindow() {
	if (!IsActive()) {
		return;
	}

	auto displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x / 20.0f, displaySize.y / 20.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(displaySize.x / 2.5f, displaySize.y / 2.5f), ImGuiCond_FirstUseEver);
	float opacityFrac = *opacityPct / 100.0f;
	ImGui::SetNextWindowBgAlpha(opacityFrac);

	auto flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
	if (!*showTitleBar) {
		flags |= ImGuiWindowFlags_NoTitleBar;
	}
	ImGui::Begin("Goal Predictor", &isWindowOpen_, flags);

	// Load font if it was queued during LoadRenderer()
	if (!emojiFont) {
		auto gui = gameWrapper->GetGUIManager();
		emojiFont = gui.GetFont(EMOJI_FONT_NAME);
		if (emojiFont) {
			LOG("Installed emoji font");
		}
	}

	auto contentRegion = ImGui::GetContentRegionAvail();
	ImVec2 cursorPos = ImGui::GetCursorScreenPos();

	double tMax = lastGameTimeMs + (lastTickWorldTimeMs - lastGameTimeWorldTimeMs);
	double tMin = tMax - *graphHistoryMs;

	const float graphWidth = contentRegion.x - (GAUGE_WIDTH + GAUGE_PADDING);
	const float sharedHeight = contentRegion.y - EMOJI_ZONE_HEIGHT;

	// --- Draw Main Graph ---
	{
		ImVec2 canvasSize(graphWidth, sharedHeight);
		GraphContext ctx;
		ctx.pMin = cursorPos;
		ctx.pMax = ImVec2(ctx.pMin.x + canvasSize.x, ctx.pMin.y + canvasSize.y);
		ctx.size = canvasSize;
		ctx.tMin = tMin;
		ctx.tMax = tMax;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(ctx.pMin, ctx.pMax, true);

		DrawGrid(drawList, ctx);
		DrawEventLines(drawList, ctx, gameDataTracker);
		DrawPredictions(drawList, ctx, gameDataTracker);
		TryDrawGraphTooltip(drawList, ctx, gameDataTracker);

		drawList->PopClipRect();
	}

	// --- Draw Live Prediction Gauge ---
	{
		GraphContext ctx;
		ctx.pMin = ImVec2(cursorPos.x + graphWidth + GAUGE_PADDING, cursorPos.y);
		ctx.pMax = ImVec2(ctx.pMin.x + GAUGE_WIDTH, ctx.pMin.y + sharedHeight);
		ctx.size = ImVec2(GAUGE_WIDTH, sharedHeight);
		ctx.tMin = 0;
		ctx.tMax = 1;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(ctx.pMin, ctx.pMax, true);

		DrawGauge(drawList, ctx, gameDataTracker, tMax);

		drawList->PopClipRect();
	}

	// Reserve space
	ImGui::Dummy(ImVec2(contentRegion.x, sharedHeight));

	// --- Draw Emoji Strip ---
	{
		ImVec2 emojiSize(graphWidth, (float)EMOJI_ZONE_HEIGHT);
		GraphContext ctx;
		ctx.pMin = ImGui::GetCursorScreenPos(); // Get fresh pos after Dummy
		ctx.pMax = ImVec2(ctx.pMin.x + emojiSize.x, ctx.pMin.y + emojiSize.y);
		ctx.size = emojiSize;
		ctx.tMin = tMin;
		ctx.tMax = tMax;

		ImGui::Dummy(emojiSize);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(ctx.pMin, ctx.pMax, true);

		DrawEmojiBar(drawList, ctx, gameDataTracker, *logPredictionTime);

		drawList->PopClipRect();
	}

	ImGui::End();
}

void GoalPredictor::RenderSettings() {
	if (!enabledCvar || !showTitleBarCvar || !opacityPctCvar || !graphHistoryMsCvar || !augmentationCvar) {
		ImGui::TextUnformatted("Loading...");
		return;
	}

	bool _enabled = *enabled;
	if (ImGui::Checkbox("Enable plugin", &_enabled)) {
		enabledCvar->setValue(_enabled);
	}

	ImGui::Separator();

	ImGui::SetWindowFontScale(1.25f);
	ImGui::Text("INTERFACE");
	ImGui::SetWindowFontScale(1.0f);

	ImGui::NewLine();

	bool _showTitleBar = *showTitleBar;
	if (ImGui::Checkbox("Show Title Bar", &_showTitleBar)) {
		showTitleBarCvar->setValue(_showTitleBar);
	}

	ImGui::NewLine();

	int _opacityPct = *opacityPct;
	if (ImGui::SliderInt("Background Opacity %", &_opacityPct, MIN_OPACITY, MAX_OPACITY)) {
		opacityPctCvar->setValue(_opacityPct);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset to default##opacity")) {
		opacityPctCvar->setValue(DEFAULT_OPACITY);
	}

	ImGui::NewLine();

	int _graphHistoryMs = *graphHistoryMs;
	if (ImGui::SliderInt("Graph History (milliseconds)", &_graphHistoryMs, MIN_GRAPH_HISTORY, MAX_GRAPH_HISTORY)) {
		graphHistoryMsCvar->setValue(_graphHistoryMs);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset to default##graphHistory")) {
		graphHistoryMsCvar->setValue(DEFAULT_GRAPH_HISTORY);
	}

	ImGui::NewLine();

	ImGui::Separator();

	ImGui::SetWindowFontScale(1.25f);
	ImGui::Text("PERFORMANCE");
	ImGui::SetWindowFontScale(1.0f);

	ImGui::NewLine();

	int _augmentation = *augmentation;
	ImGui::Text("Model Inference Augmentation:");
	ImGui::SameLine();
	if (ImGui::RadioButton("1x", &_augmentation, (int)NO_AUGMENT)) {
		augmentationCvar->setValue(_augmentation);
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("2x", &_augmentation, (int)AUGMENT_2X)) {
		augmentationCvar->setValue(_augmentation);
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("4x", &_augmentation, (int)AUGMENT_4X)) {
		augmentationCvar->setValue(_augmentation);
	}
	ImGui::TextWrapped("The model can give slightly better results by making multiple predictions on mirrored data and averaging them, but it's more work on the CPU.");
	ImGui::TextWrapped("If you're experiencing performance issues, try reducing this setting.");

	ImGui::NewLine();

	ImGui::Separator();

	ImGui::SetWindowFontScale(1.25f);
	ImGui::Text("DETAILS");
	ImGui::SetWindowFontScale(1.0f);

	ImGui::NewLine();

	ImGui::TextWrapped("This plugin predicts the probability for each team to score within the next 10 seconds based on RLCS training data.");
	ImGui::TextWrapped("It only works in 3v3 matches as a spectator in an online game or while watching a replay.");

	ImGui::NewLine();

	ImGui::TextWrapped(
		"Big boost orb and player respawn timers are inputs to the model, but we need to have seen the original boost pickup or demolition to infer the timer. "
		"Predictions may be slightly skewed when fast-forwarding while viewing a replay.");
	ImGui::TextWrapped(
		"The model also has no understanding of zero-second behavior (or the current score) so predictions around that time are imperfect. "
		"The plugin draws a yellow line within 10 seconds left in regulation time to indicate this.");
	ImGui::TextWrapped("For some reason, we unfortunately cannot detect some ball touches; this only impacts the UI, not the model itself.");
}