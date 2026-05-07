#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace iHTMLChrome {
namespace CEF {
struct FrameBuffer
{
	int width = 0;
	int height = 0;
	std::uint64_t sequence = 0;
	std::vector<std::uint8_t> rgba;
};

struct BrowserRect
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

enum class InputType
{
	MouseMove,
	MouseDown,
	MouseUp,
	MouseWheel,
	KeyDown,
	KeyUp,
	KeyChar,
	TouchDown,
	TouchUp,
	TouchMove
};

struct InputEvent
{
	InputType type = InputType::MouseMove;
	int x = 0;
	int y = 0;
	int delta_x = 0;
	int delta_y = 0;
	int button = 0;
	int key = 0;
	std::uint32_t codepoint = 0;
	int pointer_id = 0;
};
} // namespace CEF
} // namespace iHTMLChrome
