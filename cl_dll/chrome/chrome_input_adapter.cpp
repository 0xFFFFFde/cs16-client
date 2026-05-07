#include "chrome_input_adapter.h"
#include "chrome_instance.h"

namespace iHTMLChrome {
namespace CEF {
InputEvent InputAdapter::MakeMouseMove( int screen_x, int screen_y, const BrowserRect& viewport, int browser_w, int browser_h ) const
{
	InputEvent ev;
	ev.type = InputType::MouseMove;
	ScreenToBrowser( screen_x, screen_y, viewport, browser_w, browser_h, ev.x, ev.y );
	return ev;
}

InputEvent InputAdapter::MakeMouseButton( bool down, int button, int screen_x, int screen_y, const BrowserRect& viewport, int browser_w, int browser_h ) const
{
	InputEvent ev;
	ev.type = down ? InputType::MouseDown : InputType::MouseUp;
	ev.button = button;
	ScreenToBrowser( screen_x, screen_y, viewport, browser_w, browser_h, ev.x, ev.y );
	return ev;
}

InputEvent InputAdapter::MakeMouseWheel( int delta, int screen_x, int screen_y, const BrowserRect& viewport, int browser_w, int browser_h ) const
{
	InputEvent ev;
	ev.type = InputType::MouseWheel;
	ev.delta_y = delta;
	ScreenToBrowser( screen_x, screen_y, viewport, browser_w, browser_h, ev.x, ev.y );
	return ev;
}

InputEvent InputAdapter::MakeKey( bool down, int key ) const
{
	InputEvent ev;
	ev.type = down ? InputType::KeyDown : InputType::KeyUp;
	ev.key = key;
	return ev;
}

InputEvent InputAdapter::MakeChar( std::uint32_t codepoint ) const
{
	InputEvent ev;
	ev.type = InputType::KeyChar;
	ev.codepoint = codepoint;
	return ev;
}

InputEvent InputAdapter::MakeTouch( InputType type, int pointer_id, int screen_x, int screen_y, const BrowserRect& viewport, int browser_w, int browser_h ) const
{
	InputEvent ev;
	ev.type = type;
	ev.pointer_id = pointer_id;
	ScreenToBrowser( screen_x, screen_y, viewport, browser_w, browser_h, ev.x, ev.y );
	return ev;
}

void InputAdapter::Dispatch( ChromeInstance& instance, const InputEvent& event ) const
{
	instance.EnqueueInput( event );
}

void InputAdapter::ScreenToBrowser( int screen_x, int screen_y, const BrowserRect& viewport, int browser_w, int browser_h, int& out_x, int& out_y ) const
{
	if( viewport.width <= 0 || viewport.height <= 0 )
	{
		out_x = 0;
		out_y = 0;
		return;
	}

	int local_x = screen_x - viewport.x;
	int local_y = screen_y - viewport.y;

	if( local_x < 0 )
		local_x = 0;
	if( local_y < 0 )
		local_y = 0;
	if( local_x >= viewport.width )
		local_x = viewport.width - 1;
	if( local_y >= viewport.height )
		local_y = viewport.height - 1;

	const int bw = browser_w > 0 ? browser_w : viewport.width;
	const int bh = browser_h > 0 ? browser_h : viewport.height;

	if( viewport.width > 1 && bw > 1 )
		out_x = ( local_x * ( bw - 1 ) ) / ( viewport.width - 1 );
	else
		out_x = 0;

	if( viewport.height > 1 && bh > 1 )
		out_y = ( local_y * ( bh - 1 ) ) / ( viewport.height - 1 );
	else
		out_y = 0;

	if( out_x < 0 )
		out_x = 0;
	if( out_y < 0 )
		out_y = 0;
	if( out_x >= bw )
		out_x = bw - 1;
	if( out_y >= bh )
		out_y = bh - 1;
}
} // namespace CEF
} // namespace iHTMLChrome
