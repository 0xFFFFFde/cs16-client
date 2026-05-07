#include "chrome_backend.h"

#if defined(CS16CLIENT_ENABLE_CEF)

#include "chrome_debug.h"
#include <cmath>
#include <cstring>
#include <string>

#include "chrome_instance.h"

#include "cef_browser.h"
#include "cef_client.h"
#include "cef_parser.h"
#include "cef_render_handler.h"
#if defined(XASH_APPLE)
#include "internal/cef_types_mac.h"
#endif
#include "keydefs.h"

namespace iHTMLChrome {
namespace CEF {

static std::string NormalizeLoadUrlForCEF( const std::string& url )
{
	static const char kDataHtmlBase64Prefix[] = "data:text/html;base64,";
	static const char kDataHtmlPrefix[] = "data:text/html,";
	if( url.compare( 0, strlen( kDataHtmlBase64Prefix ), kDataHtmlBase64Prefix ) == 0 )
		return url;
	if( url.compare( 0, strlen( kDataHtmlPrefix ), kDataHtmlPrefix ) == 0 )
	{
		const std::string html = url.substr( strlen( kDataHtmlPrefix ) );
		const std::string html_base64 = CefBase64Encode( html.data(), html.size() ).ToString();
		return std::string( "data:text/html;base64," ) + html_base64;
	}
	return url;
}

namespace
{
// Chromium/CEF: zoom factor ≈ 1.2^level; level 0 => 100%.
constexpr double kDefaultPageScale = 1.0;
double g_browser_page_scale = kDefaultPageScale;
int g_chrome_windowless_target_fps = 60;

double PageScaleToCEFZoomLevel( double scale )
{
	if( scale <= 0.001 )
		scale = 0.001;
	double level = std::log( scale ) / std::log( 1.2 );
	if( level < -10.0 )
		level = -10.0;
	if( level > 10.0 )
		level = 10.0;
	return level;
}

int EngineKeyToCEFKey( int key )
{
	if( key >= 'a' && key <= 'z' )
		return key - 'a' + 'A';
	if( key >= 32 && key <= 126 )
		return key;

	switch( key )
	{
	case K_BACKSPACE: return 0x08; // VK_BACK
	case K_DEL: return 0x08; // VK_DELETE
	case K_TAB: return 0x09; // VK_TAB
	case K_ENTER:
	case K_KP_ENTER: return 0x0D; // VK_RETURN
#if defined(XASH_APPLE)
	case K_WIN: return 0x0D; // VK_RETURN
#endif
	case K_SHIFT: return 0x10; // VK_SHIFT
	case K_CTRL: return 0x11; // VK_CONTROL
	case K_ALT: return 0x12; // VK_MENU
	case K_ESCAPE: return 0x1B; // VK_ESCAPE
	case K_SPACE: return 0x20; // VK_SPACE
	case K_PGUP: return 0x21; // VK_PRIOR
	case K_PGDN: return 0x22; // VK_NEXT
	case K_END: return 0x23; // VK_END
	case K_HOME: return 0x24; // VK_HOME
	case K_LEFTARROW: return 0x25; // VK_LEFT
	case K_UPARROW: return 0x26; // VK_UP
	case K_RIGHTARROW: return 0x27; // VK_RIGHT
	case K_DOWNARROW: return 0x28; // VK_DOWN
	case K_KP_HOME: return 0x24; // VK_HOME
	case K_KP_UPARROW: return 0x26; // VK_UP
	case K_KP_PGUP: return 0x21; // VK_PRIOR
	case K_KP_LEFTARROW: return 0x25; // VK_LEFT
	case K_KP_RIGHTARROW: return 0x27; // VK_RIGHT
	case K_KP_END: return 0x23; // VK_END
	case K_KP_DOWNARROW: return 0x28; // VK_DOWN
	case K_KP_PGDN: return 0x22; // VK_NEXT
	case K_KP_DEL: return 0x08; // match K_DEL → backspace semantics
	case K_INS: return 0x2D; // VK_INSERT
	case K_KP_INS: return 0x2D; // VK_INSERT
	case K_F1: return 0x70; // VK_F1
	case K_F2: return 0x71;
	case K_F3: return 0x72;
	case K_F4: return 0x73;
	case K_F5: return 0x74;
	case K_F6: return 0x75;
	case K_F7: return 0x76;
	case K_F8: return 0x77;
	case K_F9: return 0x78;
	case K_F10: return 0x79;
	case K_F11: return 0x7A;
	case K_F12: return 0x7B; // VK_F12
	default: return key;
	}
}

void BuildCEFKeyCodes( int key, int& out_windows_key_code, int& out_native_key_code )
{
	// This input path receives engine key codes, not OS-native scan codes.
	// Treat values consistently as engine keys to avoid accidental remaps
	// (e.g. engine Enter=13 being interpreted as mac native keycode 13 -> 'W').
	out_windows_key_code = EngineKeyToCEFKey( key );
	out_native_key_code = 0;
}

class CEFRenderClient final : public CefClient, public CefLifeSpanHandler, public CefRenderHandler, public CefLoadHandler
{
public:
	explicit CEFRenderClient( ChromeInstance* instance ) : instance_( instance )
	{
	}

	void DetachInstance()
	{
		instance_ = nullptr;
	}

	void SetViewSize( int width, int height )
	{
		width_ = width > 0 ? width : 1;
		height_ = height > 0 ? height : 1;
	}

	void LoadURL( const std::string& url )
	{
		pending_url_ = url;
		if( browser_ && browser_->GetMainFrame() )
			browser_->GetMainFrame()->LoadURL( NormalizeLoadUrlForCEF( pending_url_ ) );
	}

	void Resize()
	{
		if( browser_ && browser_->GetHost() )
		{
			browser_->GetHost()->WasResized();
			browser_->GetHost()->Invalidate( PET_VIEW );
		}
	}

	void Close()
	{
		if( browser_ && browser_->GetHost() )
			browser_->GetHost()->CloseBrowser( true );
	}

	void SendInput( const InputEvent& event )
	{
		if( !instance_ || !browser_ || !browser_->GetHost() )
			return;

		CefRefPtr<CefBrowserHost> host = browser_->GetHost();
		CefMouseEvent mouse_event;
		mouse_event.x = event.x;
		mouse_event.y = event.y;

		switch( event.type )
		{
		case InputType::MouseMove:
			host->SendMouseMoveEvent( mouse_event, false );
			break;
		case InputType::MouseDown:
		case InputType::MouseUp:
		{
			host->SetFocus( true );
			CefBrowserHost::MouseButtonType button = MBT_LEFT;
			if( event.button == 1 )
				button = MBT_RIGHT;
			else if( event.button == 2 )
				button = MBT_MIDDLE;
			host->SendMouseClickEvent( mouse_event, button, event.type == InputType::MouseUp, 1 );
			break;
		}
		case InputType::MouseWheel:
			host->SendMouseWheelEvent( mouse_event, event.delta_x, event.delta_y );
			break;
		case InputType::KeyDown:
		case InputType::KeyUp:
		{
			host->SetFocus( true );
			int windows_key_code = 0;
			int native_key_code = 0;
			BuildCEFKeyCodes( event.key, windows_key_code, native_key_code );
			CefKeyEvent key_event;
			key_event.type = event.type == InputType::KeyDown ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
			key_event.windows_key_code = windows_key_code;
			key_event.native_key_code = native_key_code;
			key_event.character = 0;
			key_event.unmodified_character = 0;
			key_event.focus_on_editable_field = true;
			host->SendKeyEvent( key_event );
			break;
		}
		case InputType::KeyChar:
		{
			host->SetFocus( true );
			CefKeyEvent key_event;
			key_event.type = KEYEVENT_CHAR;
			key_event.windows_key_code = static_cast<int>( event.codepoint );
			key_event.native_key_code = 0;
			key_event.character = static_cast<char16_t>( event.codepoint );
			key_event.unmodified_character = static_cast<char16_t>( event.codepoint );
			key_event.focus_on_editable_field = true;
			host->SendKeyEvent( key_event );
			break;
		}
		case InputType::TouchDown:
		case InputType::TouchMove:
		case InputType::TouchUp:
			// Keep mobile support functional even before full CefTouchEvent mapping.
			if( event.type == InputType::TouchMove )
				host->SendMouseMoveEvent( mouse_event, false );
			else
				host->SendMouseClickEvent( mouse_event, MBT_LEFT, event.type == InputType::TouchUp, 1 );
			break;
		}
	}

	void ApplyZoomLevel()
	{
		if( !browser_ || !browser_->GetHost() )
			return;
		if( std::abs( last_applied_page_scale_ - g_browser_page_scale ) < 1e-6 )
			return;
		last_applied_page_scale_ = g_browser_page_scale;
		const double want = PageScaleToCEFZoomLevel( g_browser_page_scale );
		applied_cef_zoom_valid_ = true;
		applied_cef_zoom_level_ = want;
		CefRefPtr<CefBrowserHost> host = browser_->GetHost();
		host->SetZoomLevel( want );
		host->WasResized();
		host->Invalidate( PET_VIEW );
	}

	void ApplyFrameRate()
	{
		if( !browser_ || !browser_->GetHost() )
			return;
		const int want = g_chrome_windowless_target_fps;
		if( applied_windowless_fps_ == want )
			return;
		applied_windowless_fps_ = want;
		browser_->GetHost()->SetWindowlessFrameRate( want );
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

	void GetViewRect( CefRefPtr<CefBrowser>, CefRect& rect ) override
	{
		rect = CefRect( 0, 0, width_, height_ );
	}

	void OnPaint( CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width, int height ) override
	{
		if( type != PET_VIEW || !instance_ || !buffer || width <= 0 || height <= 0 )
			return;

		++frame_sequence_;
		instance_->OnCefPaint( static_cast<const std::uint8_t*>( buffer ), width, height, frame_sequence_ );
	}

	void OnAcceleratedPaint( CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const CefAcceleratedPaintInfo& ) override
	{
		if( type != PET_VIEW )
			return;
	}

	void OnAfterCreated( CefRefPtr<CefBrowser> browser ) override
	{
		browser_ = browser;
		applied_cef_zoom_valid_ = false;
		applied_windowless_fps_ = -1;
		last_applied_page_scale_ = -1.0;
		if( browser_ && browser_->GetHost() )
		{
			CefRefPtr<CefBrowserHost> host = browser_->GetHost();
			const double z = PageScaleToCEFZoomLevel( g_browser_page_scale );
			last_applied_page_scale_ = g_browser_page_scale;
			host->SetZoomLevel( z );
			applied_cef_zoom_valid_ = true;
			applied_cef_zoom_level_ = z;
			host->WasResized();
			host->Invalidate( PET_VIEW );
			host->SetFocus( true );
		}
		if( !pending_url_.empty() && browser_ && browser_->GetMainFrame() )
		{
			LoadURL( pending_url_ );
		}
		if( browser_ && browser_->GetHost() )
		{
			browser_->GetHost()->WasResized();
			browser_->GetHost()->Invalidate( PET_VIEW );
		}
	}

	void OnBeforeClose( CefRefPtr<CefBrowser> ) override
	{
		browser_ = nullptr;
	}

	bool OnBeforePopup( CefRefPtr<CefBrowser>,
		CefRefPtr<CefFrame> frame,
		int,
		const CefString& target_url,
		const CefString&,
		CefLifeSpanHandler::WindowOpenDisposition,
		bool,
		const CefPopupFeatures&,
		CefWindowInfo&,
		CefRefPtr<CefClient>&,
		CefBrowserSettings&,
		CefRefPtr<CefDictionaryValue>&,
		bool* ) override
	{
		if( frame )
			frame->LoadURL( target_url );
		else if( browser_ && browser_->GetMainFrame() )
			browser_->GetMainFrame()->LoadURL( target_url );
		return true; // Block native popup window creation.
	}

	void OnLoadingStateChange( CefRefPtr<CefBrowser>, bool, bool, bool ) override {}

	void OnLoadEnd( CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int ) override
	{
		// Chromium resets page zoom on navigation; re-apply cl_chrome_zoom (g_browser_page_scale).
		if( !frame || !frame->IsMain() || !browser_ )
			return;
		CefRefPtr<CefBrowserHost> host = browser_->GetHost();
		if( !host )
			return;
		const double z = PageScaleToCEFZoomLevel( g_browser_page_scale );
		last_applied_page_scale_ = g_browser_page_scale;
		host->SetZoomLevel( z );
		applied_cef_zoom_valid_ = true;
		applied_cef_zoom_level_ = z;
		host->WasResized();
		host->Invalidate( PET_VIEW );
	}

	void OnLoadError( CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& error_text, const CefString& failed_url ) override
	{
		if( !frame || !frame->IsMain() )
			return;
		if( static_cast<int>( code ) == -3 )
			return;
		ChromeDebug_DPrintf( 1.f, "[browser] CEF load: main frame load error code=%d text='%s' url='%s'\n",
			static_cast<int>( code ), error_text.ToString().c_str(), failed_url.ToString().c_str() );
	}

private:
	ChromeInstance* instance_ = nullptr;
	CefRefPtr<CefBrowser> browser_;
	std::string pending_url_;
	int width_ = 1;
	int height_ = 1;
	std::uint64_t frame_sequence_ = 0;
	bool applied_cef_zoom_valid_ = false;
	double applied_cef_zoom_level_ = 0.0;
	double last_applied_page_scale_ = -1.0;
	int applied_windowless_fps_ = -1;

	IMPLEMENT_REFCOUNTING( CEFRenderClient );
};
} // namespace

class CEFBackend final : public ChromeBackend
{
public:
	bool Initialize( ChromeInstance& instance, const char* start_url ) override
	{
		instance_ = &instance;
		client_ = new CEFRenderClient( instance_ );

		CefWindowInfo window_info;
#if defined(_WIN32)
		window_info.SetAsWindowless( nullptr );
#else
		window_info.SetAsWindowless( 0 );
		window_info.shared_texture_enabled = 0;
#endif

		CefBrowserSettings settings;
		settings.windowless_frame_rate = g_chrome_windowless_target_fps;
		settings.background_color = CefColorSetARGB( 255, 255, 255, 255 );
		client_->SetViewSize( width_, height_ );

		std::string initial = "about:blank";
		if( start_url && start_url[0] )
			initial = NormalizeLoadUrlForCEF( std::string( start_url ) );

		CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
			window_info, client_, initial, settings, nullptr, nullptr );

		if( browser.get() == nullptr )
		{
			client_ = nullptr;
			return false;
		}

		if( browser->GetHost() )
		{
			browser->GetHost()->WasResized();
			browser->GetHost()->Invalidate( PET_VIEW );
		}

		return true;
	}

	void Shutdown() override
	{
		if( client_ )
		{
			client_->DetachInstance();
			client_->Close();
			client_ = nullptr;
		}
		instance_ = nullptr;
	}

	void LoadURL( const std::string& url ) override
	{
		if( client_ )
		{
			client_->LoadURL( url );
			client_->Resize();
		}
	}

	void Resize( int width, int height ) override
	{
		width_ = width;
		height_ = height;
		if( client_ )
		{
			client_->SetViewSize( width_, height_ );
			client_->Resize();
		}
	}

	void HandleInput( const InputEvent& event ) override
	{
		if( client_ )
			client_->SendInput( event );
	}

	void Tick() override
	{
		if( client_ )
		{
			client_->ApplyZoomLevel();
			client_->ApplyFrameRate();
		}
	}

private:
	ChromeInstance* instance_ = nullptr;
	CefRefPtr<CEFRenderClient> client_;
	int width_ = 0;
	int height_ = 0;
};

std::unique_ptr<ChromeBackend> CreateChromeBackend()
{
	return std::make_unique<CEFBackend>();
}

void SetChromeZoom( double scale )
{
	if( scale < 0.05 )
		scale = 0.05;
	if( scale > 5.0 )
		scale = 5.0;
	g_browser_page_scale = scale;
}

void SetChromeFrameRate( int frames_per_second )
{
	if( frames_per_second < 1 )
		frames_per_second = 1;
	if( frames_per_second > 240 )
		frames_per_second = 240;
	g_chrome_windowless_target_fps = frames_per_second;
}
} // namespace CEF
} // namespace iHTMLChrome
#endif
