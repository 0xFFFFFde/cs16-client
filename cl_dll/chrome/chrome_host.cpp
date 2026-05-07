#include "chrome_host.h"
#include "chrome_debug.h"
#include "chrome_backend.h"
#include "chrome_instance.h"
#include "chrome_runtime.h"
#include "chrome_input_adapter.h"
#include "chrome_types.h"

#include "hud.h"
#include "cl_util.h"
#include "triangleapi.h"
#include "draw_util.h"
#include "keydefs.h"
#include "render_api.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <cstdint>

namespace
{
using namespace iHTMLChrome::CEF;

enum class OverlayKind
{
	None,
	Motd,
	Overlay
};

struct HostState
{
	bool init_ok = false;
	OverlayKind kind = OverlayKind::None;
	std::unique_ptr<ChromeInstance> browser;

	int last_mouse_x = 0;
	int last_mouse_y = 0;
	bool has_last_mouse = false;
	bool shift_down = false;
	bool ctrl_down = false;
	bool alt_down = false;
	bool capslock_on = false;

	int overlay_mouse_mstate_prev = -1;

	InputAdapter input;
};

HostState g_host;

cvar_t* cl_chrome_scale = nullptr;
cvar_t* cl_chrome_zoom = nullptr;
cvar_t* cl_chrome_framerate = nullptr;

bool PointInRect( int x, int y, const BrowserRect& rect )
{
	return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

void GetRenderSize( int& w, int& h )
{
	w = ScreenWidth;
	h = ScreenHeight;
	if( gRenderAPI.RenderGetParm )
	{
		const int rw = static_cast<int>( gRenderAPI.RenderGetParm( PARM_SCREEN_WIDTH, 0 ) );
		const int rh = static_cast<int>( gRenderAPI.RenderGetParm( PARM_SCREEN_HEIGHT, 0 ) );
		if( rw > 0 )
			w = rw;
		if( rh > 0 )
			h = rh;
	}
}


void MapEngineMouseToRenderPixels( int mx, int my, int& rx, int& ry )
{
	int render_w = ScreenWidth;
	int render_h = ScreenHeight;
	GetRenderSize( render_w, render_h );

	const int vw = ScreenWidth > 0 ? ScreenWidth : render_w;
	const int vh = ScreenHeight > 0 ? ScreenHeight : render_h;

	if( vw <= 0 || vh <= 0 || render_w <= 0 || render_h <= 0 )
	{
		rx = mx;
		ry = my;
		return;
	}

	if( vw == render_w && vh == render_h )
	{
		rx = mx;
		ry = my;
		return;
	}

#if defined(XASH_APPLE)
	rx = mx;
	ry = my;
#else
	rx = static_cast<int>( ( static_cast<std::int64_t>( mx ) * render_w + ( vw / 2 ) ) / vw );
	ry = static_cast<int>( ( static_cast<std::int64_t>( my ) * render_h + ( vh / 2 ) ) / vh );
#endif

	if( rx < 0 )
		rx = 0;
	else if( rx >= render_w )
		rx = render_w - 1;

	if( ry < 0 )
		ry = 0;
	else if( ry >= render_h )
		ry = render_h - 1;
}

float CvarValue( cvar_t* var, float fallback )
{
	return var ? var->value : fallback;
}

std::string Base64Encode( const unsigned char* data, std::size_t len )
{
	static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve( ( ( len + 2 ) / 3 ) * 4 );
	for( std::size_t i = 0; i < len; i += 3 )
	{
		const unsigned int octet_a = i < len ? data[i] : 0;
		const unsigned int octet_b = i + 1 < len ? data[i + 1] : 0;
		const unsigned int octet_c = i + 2 < len ? data[i + 2] : 0;
		const unsigned int triple = ( octet_a << 16 ) + ( octet_b << 8 ) + octet_c;
		out.push_back( kTable[( triple >> 18 ) & 63] );
		out.push_back( kTable[( triple >> 12 ) & 63] );
		if( i + 1 < len )
			out.push_back( kTable[( triple >> 6 ) & 63] );
		else
			out.push_back( '=' );
		if( i + 2 < len )
			out.push_back( kTable[ triple & 63] );
		else
			out.push_back( '=' );
	}
	return out;
}

std::string BuildDataHtmlUrl( const char* html )
{
	if( !html || !html[0] )
		return "about:blank";

	static constexpr char kDataBase64Prefix[] = "data:text/html;base64,";
	const std::string b64 = Base64Encode( reinterpret_cast<const unsigned char*>( html ), strlen( html ) );
	return std::string( kDataBase64Prefix ) + b64;
}

std::uint32_t BrowserKeyToCodepoint( int keynum, bool shift_down, bool capslock_on )
{
#if defined(XASH_APPLE)
	if( keynum == K_WIN )
		return 0;
#endif
	if( ( keynum >= K_UPARROW && keynum <= K_END ) || ( keynum >= K_KP_HOME && keynum <= K_KP_DEL ) )
		return 0;
	if( keynum == K_BACKSPACE || keynum == K_DEL || keynum == K_ENTER || keynum == K_KP_ENTER )
		return 0;
	if( keynum == K_TAB )
		return '\t';
	if( keynum == K_SPACE )
		return ' ';

	if( ( keynum >= 'a' && keynum <= 'z' ) || ( keynum >= 'A' && keynum <= 'Z' ) )
	{
		const int lower = ( keynum >= 'A' && keynum <= 'Z' ) ? ( keynum - 'A' + 'a' ) : keynum;
		const bool uppercase = shift_down ^ capslock_on;
		return static_cast<std::uint32_t>( uppercase ? ( lower - 'a' + 'A' ) : lower );
	}

	if( keynum >= '0' && keynum <= '9' )
	{
		if( !shift_down )
			return static_cast<std::uint32_t>( keynum );

		static const char kShiftDigits[] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };
		return static_cast<std::uint32_t>( kShiftDigits[keynum - '0'] );
	}

	switch( keynum )
	{
	case '`': return static_cast<std::uint32_t>( shift_down ? '~' : '`' );
	case '-': return static_cast<std::uint32_t>( shift_down ? '_' : '-' );
	case '=': return static_cast<std::uint32_t>( shift_down ? '+' : '=' );
	case '[': return static_cast<std::uint32_t>( shift_down ? '{' : '[' );
	case ']': return static_cast<std::uint32_t>( shift_down ? '}' : ']' );
	case '\\': return static_cast<std::uint32_t>( shift_down ? '|' : '\\' );
	case ';': return static_cast<std::uint32_t>( shift_down ? ':' : ';' );
	case '\'': return static_cast<std::uint32_t>( shift_down ? '"' : '\'' );
	case ',': return static_cast<std::uint32_t>( shift_down ? '<' : ',' );
	case '.': return static_cast<std::uint32_t>( shift_down ? '>' : '.' );
	case '/': return static_cast<std::uint32_t>( shift_down ? '?' : '/' );
	default: break;
	}

	return 0;
}

void EnsureChromeCvars()
{
	ChromeDebug_Init();

	if( cl_chrome_scale )
		return;

	cl_chrome_scale = CVAR_CREATE( "cl_chrome_scale", "1.25", FCVAR_ARCHIVE );
	cl_chrome_zoom = CVAR_CREATE( "cl_chrome_zoom", "1", FCVAR_ARCHIVE );
	cl_chrome_framerate = CVAR_CREATE( "cl_chrome_framerate", "30", FCVAR_ARCHIVE );
}

bool CanOpenChromeInCurrentState()
{
	if( !gEngfuncs.GetLocalPlayer || !gEngfuncs.GetLocalPlayer() )
		return false;
	if( !gEngfuncs.pfnGetLevelName )
		return false;
	const char* level_name = gEngfuncs.pfnGetLevelName();
	return level_name && level_name[0] != '\0';
}

constexpr char kDefaultBrowserUrl[] = "https://google.com/";
constexpr int kOverlayHeaderHeight = 44;
constexpr int kOverlayBottomBarHeight = 150;
constexpr int kOverlayViewportMaxWidthPercent = 78;
constexpr int kOverlayViewportMaxHeightPercent = 68;
constexpr int kDefaultBrowserLogicalWidth = 1280;
constexpr int kDefaultBrowserLogicalHeight = 720;
constexpr int kOverlayPadding = 0;

BrowserRect ComputeCenteredViewport()
{
	BrowserRect viewport{};
	int render_width = 0;
	int render_height = 0;
	GetRenderSize( render_width, render_height );
	const float scale = CvarValue( cl_chrome_scale, 1.0f );
	const int header_h = kOverlayHeaderHeight;
	const int bottom_h = kOverlayBottomBarHeight;

	const int want_max_w = static_cast<int>( ( render_width * kOverlayViewportMaxWidthPercent ) / 100.0f * scale );
	const int want_max_h = static_cast<int>( ( render_height * kOverlayViewportMaxHeightPercent ) / 100.0f * scale );
	const int max_w = want_max_w < render_width ? want_max_w : render_width;
	const int max_h = want_max_h < render_height ? want_max_h : render_height;

	int view_w = max_w;
	int view_h = ( view_w * kDefaultBrowserLogicalHeight ) / kDefaultBrowserLogicalWidth;
	if( view_h > max_h )
	{
		view_h = max_h;
		view_w = ( view_h * kDefaultBrowserLogicalWidth ) / kDefaultBrowserLogicalHeight;
	}

	viewport.width = view_w;
	viewport.height = view_h;
	const int panel_w = viewport.width + ( kOverlayPadding * 2 );
	const int panel_h = viewport.height + ( kOverlayPadding * 2 ) + header_h + bottom_h;
	const int panel_x = ( render_width - panel_w ) / 2;
	const int panel_y = ( render_height - panel_h ) / 2;
	viewport.x = panel_x + kOverlayPadding;
	viewport.y = panel_y + kOverlayPadding + header_h;
	return viewport;
}

void SetOverlayMouseCapture( bool active )
{
	static bool capture = false;
	// static bool touch_client_only_prev_valid = false;
	// static bool touch_client_only_prev = false;
	// static bool touch_client_only_forced = false;
	if( capture == active )
		return;

	capture = active;

	if( gEngfuncs.pfnSetMouseEnable )
		gEngfuncs.pfnSetMouseEnable( active ? 0 : 1 );

	// cvar_t* touch_client_only = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer( "touch_setclientonly" ) : nullptr;
	// if( active )
	// {
	// 	touch_client_only_prev = ( touch_client_only && touch_client_only->value > 0.0f );
	// 	touch_client_only_prev_valid = true;

	// 	if( !touch_client_only_prev && gEngfuncs.pfnClientCmd )
	// 	{
	// 		gEngfuncs.pfnClientCmd( "touch_setclientonly 1\n" );
	// 		touch_client_only_forced = true;
	// 	}
	// 	else
	// 	{
	// 		touch_client_only_forced = false;
	// 	}
	// }
	// else
	// {
	// 	if( touch_client_only_forced && touch_client_only_prev_valid && !touch_client_only_prev && gEngfuncs.pfnClientCmd )
	// 		gEngfuncs.pfnClientCmd( "touch_setclientonly 0\n" );

	// 	touch_client_only_prev_valid = false;
	// 	touch_client_only_forced = false;
	// }
}

void DestroyOverlayChrome()
{
	if( !g_host.browser )
		return;

	g_host.browser->Shutdown();
	g_host.browser.reset();
	g_host.kind = OverlayKind::None;
	SetOverlayMouseCapture( false );
	g_host.has_last_mouse = false;
	g_host.overlay_mouse_mstate_prev = -1;
}

bool HostEnsureRuntime()
{
	EnsureChromeCvars();

	if( g_host.init_ok )
		return true;

	if( !CefRuntime::Get().Initialize() )
	{
		return false;
	}

	g_host.init_ok = true;
	return true;
}

void DispatchMouseMove( ChromeInstance& inst, int screen_x, int screen_y )
{
	const InputEvent ev = g_host.input.MakeMouseMove(
		screen_x, screen_y, inst.viewport(), inst.browser_width(), inst.browser_height() );
	g_host.input.Dispatch( inst, ev );
}

void DispatchMouseButton( ChromeInstance& inst, bool down, int button, int screen_x, int screen_y )
{
	const InputEvent ev = g_host.input.MakeMouseButton(
		down, button, screen_x, screen_y, inst.viewport(), inst.browser_width(), inst.browser_height() );
	g_host.input.Dispatch( inst, ev );
}

void DispatchMouseWheel( ChromeInstance& inst, int delta, int screen_x, int screen_y )
{
	const InputEvent ev = g_host.input.MakeMouseWheel(
		delta, screen_x, screen_y, inst.viewport(), inst.browser_width(), inst.browser_height() );
	g_host.input.Dispatch( inst, ev );
}

void DispatchKey( ChromeInstance& inst, bool down, int key )
{
	g_host.input.Dispatch( inst, g_host.input.MakeKey( down, key ) );
}

void DispatchChar( ChromeInstance& inst, std::uint32_t codepoint )
{
	g_host.input.Dispatch( inst, g_host.input.MakeChar( codepoint ) );
}

bool CreateOverlay( OverlayKind kind, const BrowserRect& viewport, const char* url )
{
	if( !HostEnsureRuntime() )
		return false;

	DestroyOverlayChrome();

	auto browser = std::make_unique<ChromeInstance>( 1 );
	if( !browser->Initialize( url ) )
		return false;

	browser->set_viewport( viewport );
	browser->Resize( kDefaultBrowserLogicalWidth, kDefaultBrowserLogicalHeight );

	g_host.browser = std::move( browser );
	g_host.kind = kind;
	SetOverlayMouseCapture( true );
	g_host.has_last_mouse = false;

	ChromeDebug_DPrintf( 1.f, "[chrome]: overlay created kind=%d url=%s\n",
		static_cast<int>( kind ), url ? url : "<null>" );
	return true;
}

void PollMouseForOverlay()
{
	if( !g_host.browser )
		return;

	if( !gEngfuncs.GetMousePosition )
		return;

	int mx = 0;
	int my = 0;
	gEngfuncs.GetMousePosition( &mx, &my );
	const int raw_mx = mx;
	const int raw_my = my;
	MapEngineMouseToRenderPixels( mx, my, mx, my );
	if( !g_host.has_last_mouse || mx != g_host.last_mouse_x || my != g_host.last_mouse_y )
	{
		DispatchMouseMove( *g_host.browser, mx, my );
		g_host.last_mouse_x = mx;
		g_host.last_mouse_y = my;
		g_host.has_last_mouse = true;
	}
}
} // namespace

bool Chrome_ProcessIN_MouseEvent( int mstate )
{
	using namespace iHTMLChrome::CEF;

	const bool overlay_ui = g_host.browser && g_host.kind != OverlayKind::None;
	if( ChromeDebug_Level() >= 1.f && mstate != 0 && !overlay_ui )
		ChromeDebug_DPrintf( 1.f, "[chrome][input] IN_MouseEvent mstate=0x%x but no overlay (ignored)\n", mstate );

	if( !g_host.browser || g_host.kind == OverlayKind::None )
		return false;

	ChromeInstance& inst = *g_host.browser;

	int mx = 0;
	int my = 0;
	if( gEngfuncs.GetMousePosition )
		gEngfuncs.GetMousePosition( &mx, &my );
	MapEngineMouseToRenderPixels( mx, my, mx, my );

	const int prev = g_host.overlay_mouse_mstate_prev;

	if( prev < 0 )
	{
		ChromeDebug_DPrintf( 1.f, "[chrome][input] IN_MouseEvent sync mstate=0x%x pos=(%d,%d)\n", mstate, mx, my );
		g_host.overlay_mouse_mstate_prev = mstate;
		return true;
	}

	if( ChromeDebug_Level() >= 1.f )
	{
		const unsigned edge_down = static_cast<unsigned>( mstate & ~prev );
		const unsigned edge_up = static_cast<unsigned>( prev & ~mstate );
		ChromeDebug_DPrintf( 1.f, "[chrome][input] IN_MouseEvent mstate=0x%x prev=0x%x pos=(%d,%d) edge_down=0x%x edge_up=0x%x\n",
			mstate, prev, mx, my, edge_down, edge_up );
	}

	for( int i = 0; i < 5; i++ )
	{
		const int bit = 1 << i;
		if( ( mstate & bit ) && !( prev & bit ) )
		{
			ChromeDebug_DPrintf( 1.f, "[chrome][input] dispatch mouse down btn=%d pos=(%d,%d)\n", i, mx, my );
			DispatchMouseButton( inst, true, i, mx, my );
		}
		else if( !( mstate & bit ) && ( prev & bit ) )
		{
			ChromeDebug_DPrintf( 1.f, "[chrome][input] dispatch mouse up btn=%d pos=(%d,%d)\n", i, mx, my );
			DispatchMouseButton( inst, false, i, mx, my );
		}
	}

	g_host.overlay_mouse_mstate_prev = mstate;
	return true;
}

static BrowserRect ChromeRectToBrowserRect( const ChromeRect& rect )
{
	BrowserRect out{};
	out.x = rect.x;
	out.y = rect.y;
	out.width = rect.width;
	out.height = rect.height;
	return out;
}

void Chrome_Init()
{
	EnsureChromeCvars();
}

void Chrome_Shutdown()
{
	if( !g_host.init_ok )
		return;

	DestroyOverlayChrome();
	CefRuntime::Get().Shutdown();
	g_host.init_ok = false;
	ChromeDebug_DPrintf( 1.f, "browser: CEF host shutdown\n" );
}

void Chrome_RunFrame()
{
	if( !g_host.init_ok )
		return;

	PollMouseForOverlay();
	SetChromeFrameRate( static_cast<int>( CvarValue( cl_chrome_framerate, 60.0f ) ) );
	SetChromeZoom( static_cast<double>( CvarValue( cl_chrome_zoom, 1.0f ) ) );
	CefRuntime::Get().DoMessageLoopWork();

	if( g_host.browser )
		g_host.browser->Tick();
}

void Chrome_Draw()
{
	using namespace iHTMLChrome::CEF;

	if( !g_host.browser )
		return;

	const unsigned int tex = g_host.browser->texture_id();
	if( tex == 0 )
		return;

	const BrowserRect vp = g_host.browser->viewport();

	gRenderAPI.GL_SelectTexture( 0 );
	gRenderAPI.GL_Bind( 0, tex );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->Color4f( 1.0f, 1.0f, 1.0f, 1.0f );

	// Viewport is framebuffer pixels (GetRenderSize); TriAPI quads here match sniperscope, not HUD 640*m_flScale.
	const float x1 = static_cast<float>( vp.x );
	const float y1 = static_cast<float>( vp.y );
	const float x2 = static_cast<float>( vp.x + vp.width );
	const float y2 = static_cast<float>( vp.y + vp.height );

	DrawUtils::Draw2DQuad( x1, y1, x2, y2 );
}

bool Chrome_HandleKeyEvent( int down, int keynum )
{
	using namespace iHTMLChrome::CEF;

	if( !g_host.browser || g_host.kind == OverlayKind::None )
		return false;

	ChromeInstance& inst = *g_host.browser;

	if( keynum == K_SHIFT )
		g_host.shift_down = down != 0;
	else if( keynum == K_CTRL )
		g_host.ctrl_down = down != 0;
	else if( keynum == K_ALT )
		g_host.alt_down = down != 0;
	else if( keynum == K_CAPSLOCK && down )
		g_host.capslock_on = !g_host.capslock_on;

	int mx = 0;
	int my = 0;
	if( gEngfuncs.GetMousePosition )
		gEngfuncs.GetMousePosition( &mx, &my );
	MapEngineMouseToRenderPixels( mx, my, mx, my );

	if( down && keynum == K_ESCAPE )
	{
		DestroyOverlayChrome();
		return true;
	}

	if( keynum >= K_MOUSE1 && keynum <= K_MOUSE5 )
	{
		DispatchMouseButton( inst, down != 0, keynum - K_MOUSE1, mx, my );
		return true;
	}

	if( keynum == K_MWHEELUP || keynum == K_MWHEELDOWN )
	{
		if( down )
			DispatchMouseWheel( inst, keynum == K_MWHEELUP ? 120 : -120, mx, my );
		return true;
	}

	DispatchKey( inst, down != 0, keynum );
	if( down && !g_host.ctrl_down && !g_host.alt_down )
	{
		const std::uint32_t cp = BrowserKeyToCodepoint( keynum, g_host.shift_down, g_host.capslock_on );
		if( cp != 0 )
		{
			DispatchChar( inst, cp );
		}
	}

	return true;
}

bool Chrome_HasActiveBrowser()
{
	return g_host.browser != nullptr;
}

bool Chrome_WantsInputCapture()
{
	return Chrome_HasActiveBrowser();
}

void Chrome_ToggleBrowser()
{
	EnsureChromeCvars();

	if( !g_host.init_ok )
	{
		ChromeDebug_Msg( "[chrome]: initializing browser runtime...\n" );
		if( !HostEnsureRuntime() )
		{
			ChromeDebug_Msg( "[chrome]: runtime initialization failed\n" );
			return;
		}
	}

	if( g_host.browser && g_host.kind == OverlayKind::Overlay )
	{
		DestroyOverlayChrome();
		ChromeDebug_Msg( "[chrome]: disabled\n" );
		return;
	}

	if( !CanOpenChromeInCurrentState() )
	{
		ChromeDebug_Msg( "[chrome]: unavailable in main menu\n" );
		return;
	}

	if( g_host.browser && g_host.kind == OverlayKind::Motd )
		DestroyOverlayChrome();

	const BrowserRect viewport = ComputeCenteredViewport();
	SetChromeFrameRate( static_cast<int>( CvarValue( cl_chrome_framerate, 60.0f ) ) );
	SetChromeZoom( static_cast<double>( CvarValue( cl_chrome_zoom, 1.0f ) ) );

	if( CreateOverlay( OverlayKind::Overlay, viewport, kDefaultBrowserUrl ) )
		ChromeDebug_Msg( "[chrome]: enabled (viewport=%dx%d)\n", viewport.width, viewport.height );
	else
		ChromeDebug_Msg( "[chrome]: failed to create browser\n" );
}

void Chrome_OpenUrlFromCommand()
{
	EnsureChromeCvars();

	std::string url;
	const int argc = gEngfuncs.Cmd_Argc();
	if( argc > 1 )
	{
		url = gEngfuncs.Cmd_Argv( 1 );
		for( int i = 2; i < argc; ++i )
		{
			url += ' ';
			url += gEngfuncs.Cmd_Argv( i );
		}
	}

	if( url.empty() )
	{
		ChromeDebug_Msg( "usage: cl_chrome_openurl <url>\n" );
		return;
	}

	if( !CanOpenChromeInCurrentState() )
	{
		ChromeDebug_Msg( "[chrome]: unavailable in main menu\n" );
		return;
	}

	if( !g_host.init_ok )
	{
		ChromeDebug_Msg( "[chrome]: initializing browser runtime...\n" );
		if( !HostEnsureRuntime() )
		{
			ChromeDebug_Msg( "[chrome]: runtime initialization failed\n" );
			return;
		}
	}

	SetChromeFrameRate( static_cast<int>( CvarValue( cl_chrome_framerate, 60.0f ) ) );
	SetChromeZoom( static_cast<double>( CvarValue( cl_chrome_zoom, 1.0f ) ) );

	if( g_host.browser && g_host.kind == OverlayKind::Overlay )
	{
		g_host.browser->set_viewport( ComputeCenteredViewport() );
		g_host.browser->LoadURL( url );
		g_host.has_last_mouse = false;
		ChromeDebug_Msg( "[chrome]: %s\n", url.c_str() );
		return;
	}

	if( g_host.browser && g_host.kind == OverlayKind::Motd )
		DestroyOverlayChrome();

	const BrowserRect viewport = ComputeCenteredViewport();
	if( CreateOverlay( OverlayKind::Overlay, viewport, url.c_str() ) )
		ChromeDebug_Msg( "[chrome]: enabled %s (viewport=%dx%d)\n", url.c_str(), viewport.width, viewport.height );
	else
		ChromeDebug_Msg( "[chrome]: failed to create browser\n" );
}

bool Chrome_Create( const char* url, const ChromeRect* viewport, int width, int height, int flags )
{
	(void)flags;
	BrowserRect target = viewport ? ChromeRectToBrowserRect( *viewport ) : ComputeCenteredViewport();
	const char* target_url = ( url && url[0] ) ? url : "about:blank";

	if( !CreateOverlay( OverlayKind::Overlay, target, target_url ) )
		return false;

	if( g_host.browser )
	{
		const int browser_width = width > 0 ? width : kDefaultBrowserLogicalWidth;
		const int browser_height = height > 0 ? height : kDefaultBrowserLogicalHeight;
		g_host.browser->Resize( browser_width, browser_height );
	}

	return true;
}

void Chrome_Destroy()
{
	DestroyOverlayChrome();
}

void Chrome_LoadURL( const char* url )
{
	if( !g_host.browser )
		return;

	const char* load = ( url && url[0] ) ? url : "about:blank";
	g_host.browser->LoadURL( load );
}

void Chrome_SetViewport( const ChromeRect* viewport )
{
	if( !g_host.browser || !viewport )
		return;

	g_host.browser->set_viewport( ChromeRectToBrowserRect( *viewport ) );
}

void Chrome_SendMouseMove( int screen_x, int screen_y )
{
	if( !g_host.browser )
		return;

	MapEngineMouseToRenderPixels( screen_x, screen_y, screen_x, screen_y );
	DispatchMouseMove( *g_host.browser, screen_x, screen_y );
}

void Chrome_SendMouseButton( int down, int button, int screen_x, int screen_y )
{
	if( !g_host.browser )
		return;

	MapEngineMouseToRenderPixels( screen_x, screen_y, screen_x, screen_y );
	DispatchMouseButton( *g_host.browser, down != 0, button, screen_x, screen_y );
}

void Chrome_SendMouseWheel( int delta, int screen_x, int screen_y )
{
	if( !g_host.browser )
		return;

	MapEngineMouseToRenderPixels( screen_x, screen_y, screen_x, screen_y );
	DispatchMouseWheel( *g_host.browser, delta, screen_x, screen_y );
}

void Chrome_SendKeyEvent( int down, int keynum )
{
	if( !g_host.browser )
		return;

	DispatchKey( *g_host.browser, down != 0, keynum );
}

void Chrome_SendCharEvent( unsigned int codepoint )
{
	if( !g_host.browser )
		return;

	DispatchChar( *g_host.browser, codepoint );
}

void Chrome_MotdOpenURL( const char* url )
{
	if( !HostEnsureRuntime() )
		return;

	if( g_host.browser && g_host.kind == OverlayKind::Overlay )
		DestroyOverlayChrome();

	const BrowserRect viewport = ComputeCenteredViewport();
	const char* load = ( url && url[0] ) ? url : "about:blank";

	if( CreateOverlay( OverlayKind::Motd, viewport, load ) )
		ChromeDebug_DPrintf( 1.f, "browser: MOTD url=%s\n", load );
}

void Chrome_MotdOpenHtml( const char* html )
{
	const std::string url = BuildDataHtmlUrl( html );
	Chrome_MotdOpenURL( url.c_str() );
}

void Chrome_MotdShow( bool state )
{
	if( state )
		return;

	if( g_host.kind != OverlayKind::Motd )
		return;

	DestroyOverlayChrome();
}

bool Chrome_MotdIsVisible()
{
	return g_host.browser != nullptr && g_host.kind == OverlayKind::Motd;
}

