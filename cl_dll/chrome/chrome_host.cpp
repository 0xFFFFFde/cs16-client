#include "chrome_host.h"
#include "chrome_backend.h"
#include "chrome_debug.h"
#include "chrome_instance.h"
#include "chrome_runtime.h"

#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "render_api.h"
#include "triangleapi.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

namespace
{
using namespace iHTMLChrome::CEF;

std::unordered_map<int, std::unique_ptr<ChromeInstance>> g_instances;
int g_next_instance_id = 1;
bool g_chrome_service_up = false;

cvar_t* cl_chrome_zoom = nullptr;
cvar_t* cl_chrome_framerate = nullptr;
cvar_t* cl_chrome_toggle = nullptr;

int g_toggle_demo_id = -1;
int g_openurl_demo_id = -1;

int s_toggle_sync = -1;

float CvarFloat( cvar_t* v, float fb )
{
	return v ? v->value : fb;
}

ChromeInstance* Lookup( int instance_id )
{
	const auto it = g_instances.find( instance_id );
	if( it == g_instances.end() || !it->second )
		return nullptr;
	return it->second.get();
}

void EraseInstanceRecord( int instance_id )
{
	const auto it = g_instances.find( instance_id );
	if( it == g_instances.end() )
		return;

	Chrome_DPrintf( 1.f, "[chrome] Destroy instance id=%d\n", instance_id );
	g_instances.erase( it );
}

void RegisterEngineCvars()
{
	Chrome_Init();

	if( !cl_chrome_zoom )
		cl_chrome_zoom = CVAR_CREATE( "cl_chrome_zoom", "1", FCVAR_ARCHIVE );
	if( !cl_chrome_framerate )
		cl_chrome_framerate = CVAR_CREATE( "cl_chrome_framerate", "30", FCVAR_ARCHIVE );
	if( !cl_chrome_toggle )
		cl_chrome_toggle = CVAR_CREATE( "cl_chrome_toggle", "0", FCVAR_ARCHIVE );
}

bool EnsureCefInitialized()
{
	if( g_chrome_service_up )
		return true;

	if( !iHTMLChrome::CEF::CefRuntime::Get().Initialize() )
	{
		Chrome_Printf( "[chrome] CefRuntime::Initialize failed — browser features disabled\n" );
		return false;
	}

	g_chrome_service_up = true;
	Chrome_DPrintf( 1.f, "[chrome] service started (CefInitialize ok)\n" );
	return true;
}

void Cmd_ChromeOpenUrl()
{
	if( !EnsureCefInitialized() )
		return;

	std::string url;
	const int argc = gEngfuncs.Cmd_Argc();
	for( int i = 1; i < argc; ++i )
	{
		if( i > 1 )
			url += ' ';
		url += gEngfuncs.Cmd_Argv( i );
	}

	while( !url.empty() && ( url.front() == ' ' || url.front() == '\t' ) )
		url.erase( url.begin() );
	while( !url.empty() && ( url.back() == ' ' || url.back() == '\t' ) )
		url.pop_back();

	if( url.empty() )
	{
		Chrome_Printf( "usage: cl_chrome_openurl <url>\n" );
		return;
	}

	if( g_toggle_demo_id >= 0 )
	{
		Chrome::Destroy( g_toggle_demo_id );
		g_toggle_demo_id = -1;
	}

	if( cl_chrome_toggle && cl_chrome_toggle->value >= 1.0f && gEngfuncs.Cvar_Set )
		gEngfuncs.Cvar_Set( "cl_chrome_toggle", "0" );
	s_toggle_sync = 0;

	if( g_openurl_demo_id >= 0 )
		Chrome::Destroy( g_openurl_demo_id );

	const int fid = Chrome::Create( url.c_str(), 1280, 720, nullptr, nullptr );
	if( fid < 0 )
		return;

	g_openurl_demo_id = fid;
	Chrome_DPrintf( 1.f, "[chrome] cl_chrome_openurl instance=%d url=%s\n", fid, url.c_str() );
}

void RegisterCommands()
{
	static bool registered = false;
	if( registered || !gEngfuncs.pfnAddCommand )
		return;
	registered = true;
	gEngfuncs.pfnAddCommand( "cl_chrome_openurl", Cmd_ChromeOpenUrl );
}

void SyncToggleDemoBrowser()
{
	if( !g_chrome_service_up || !cl_chrome_toggle )
		return;

	const int want = cl_chrome_toggle->value >= 1.0f ? 1 : 0;
	if( s_toggle_sync < 0 )
	{
		s_toggle_sync = want;
		return;
	}

	if( want == s_toggle_sync )
		return;

	s_toggle_sync = want;

	if( want )
	{
		if( g_openurl_demo_id >= 0 )
			Chrome::Destroy( g_openurl_demo_id );
		g_openurl_demo_id = -1;

		if( g_toggle_demo_id < 0 )
		{
			g_toggle_demo_id = Chrome::Create( "https://www.google.com/", 1280, 720, nullptr, nullptr );
			Chrome_DPrintf( 1.f, "[chrome] cl_chrome_toggle enabled demo instance=%d\n", g_toggle_demo_id );
		}
		return;
	}

	if( g_toggle_demo_id >= 0 )
	{
		const int tid = g_toggle_demo_id;
		g_toggle_demo_id = -1;
		Chrome::Destroy( tid );
		Chrome_DPrintf( 1.f, "[chrome] cl_chrome_toggle off (destroy demo)\n" );
	}
}

struct RenderExtents
{
	int w = 0;
	int h = 0;
};

RenderExtents VisibleRenderExtents()
{
	RenderExtents e;
	e.w = ScreenWidth;
	e.h = ScreenHeight;

	if( gRenderAPI.RenderGetParm )
	{
		const int rw = static_cast<int>( gRenderAPI.RenderGetParm( PARM_SCREEN_WIDTH, 0 ) );
		const int rh = static_cast<int>( gRenderAPI.RenderGetParm( PARM_SCREEN_HEIGHT, 0 ) );
		if( rw > 0 )
			e.w = rw;
		if( rh > 0 )
			e.h = rh;
	}

	return e;
}

void FitCenterLetterbox( float aspect, int fw, int fh, float& x1, float& y1, float& x2, float& y2 )
{
	if( fw <= 0 || fh <= 0 || aspect <= 0.f )
	{
		x1 = y1 = 0.f;
		x2 = static_cast<float>( fw );
		y2 = static_cast<float>( fh );
		return;
	}

	const float frame_aspect = static_cast<float>( fw ) / static_cast<float>( fh );

	float vw = static_cast<float>( fw );
	float vh = static_cast<float>( fh );
	if( frame_aspect > aspect )
		vw = vh * aspect;
	else if( frame_aspect < aspect )
		vh = vw / aspect;

	const float ox = ( static_cast<float>( fw ) - vw ) * 0.5f;
	const float oy = ( static_cast<float>( fh ) - vh ) * 0.5f;
	x1 = ox;
	y1 = oy;
	x2 = ox + vw;
	y2 = oy + vh;
}

void PresentDebugQuad( ChromeInstance* inst )
{
	if( !inst )
		return;

	const unsigned int tex = inst->texture_id();
	if( tex == 0 || !gEngfuncs.pTriAPI )
		return;

	const RenderExtents ext = VisibleRenderExtents();
	if( ext.w <= 1 || ext.h <= 1 )
		return;

	int bw = inst->width();
	int bh = inst->height();
	if( bw < 1 )
		bw = 1;
	if( bh < 1 )
		bh = 1;

	inst->set_viewport( ChromeRect{ 0, 0, bw, bh } );

	float x1, y1, x2, y2;
	FitCenterLetterbox( 16.f / 9.f, ext.w, ext.h, x1, y1, x2, y2 );

	gRenderAPI.GL_SelectTexture( 0 );
	gRenderAPI.GL_Bind( 0, tex );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->Color4f( 1.0f, 1.0f, 1.0f, 1.0f );
	DrawUtils::Draw2DQuad( x1, y1, x2, y2 );
}

} // namespace

namespace Chrome {

bool Initialize()
{
	RegisterEngineCvars();
	if( !EnsureCefInitialized() )
		return false;

	RegisterCommands();

	return true;
}

void Shutdown()
{
	g_instances.clear();
	g_toggle_demo_id = -1;
	g_openurl_demo_id = -1;

	if( !g_chrome_service_up )
		return;

	iHTMLChrome::CEF::CefRuntime::Get().Shutdown();
	g_chrome_service_up = false;
	s_toggle_sync = -1;
	Chrome_DPrintf( 1.f, "[chrome] Chrome::Shutdown complete\n" );
}

void RunFrame()
{
	if( !g_chrome_service_up )
		return;

	SetChromeZoom( static_cast<double>( CvarFloat( cl_chrome_zoom, 1.0f ) ) );
	SetChromeFrameRate( static_cast<int>( CvarFloat( cl_chrome_framerate, 30.0f ) ) );

	iHTMLChrome::CEF::CefRuntime::Get().DoMessageLoopWork();

	for( auto& kv : g_instances )
		if( kv.second )
			kv.second->Tick();

	SyncToggleDemoBrowser();
}

void PresentDebugPresentationOverlays()
{
	if( !g_chrome_service_up )
		return;

	if( g_toggle_demo_id >= 0 )
		PresentDebugQuad( Lookup( g_toggle_demo_id ) );

	if( g_openurl_demo_id >= 0 )
		PresentDebugQuad( Lookup( g_openurl_demo_id ) );
}

int Create(
	const char* url,
	const int width,
	const int height,
	const ChromeOnPaintCallback callback,
	void* userdata )
{
	if( !EnsureCefInitialized() )
		return -1;

	const int logical_w = width > 0 ? width : 1280;
	const int logical_h = height > 0 ? height : 720;

	const int id = g_next_instance_id++;

	auto inst = std::make_unique<ChromeInstance>( id );
	const char* start = ( url && url[0] ) ? url : "about:blank";

	Chrome_DPrintf(
		1.f,
		"[chrome] Create id=%d url=%s logical=%dx%d\n",
		id,
		start,
		logical_w,
		logical_h );

	if( !inst->Initialize( start ) )
	{
		Chrome_Printf( "[chrome] Chrome::Create failed during Initialize\n" );
		return -1;
	}

	if( callback )
		inst->SetPaintConsumer(
			reinterpret_cast<ChromePaintConsumerFn>( callback ), userdata );

	g_instances.emplace( id, std::move( inst ) );

	Chrome::Resize( id, logical_w, logical_h );
	return id;
}

void Destroy( const int instance_id )
{
	if( instance_id == g_toggle_demo_id )
		g_toggle_demo_id = -1;
	if( instance_id == g_openurl_demo_id )
		g_openurl_demo_id = -1;
	EraseInstanceRecord( instance_id );
}

void Resize( const int instance_id, const int width, const int height )
{
	ChromeInstance* inst = Lookup( instance_id );
	if( !inst )
		return;

	Chrome_DPrintf( 1.f, "[chrome] Resize id=%d -> %dx%d\n", instance_id, width, height );
	inst->Resize( width, height );
}

void Navigate( const int instance_id, const char* url )
{
	ChromeInstance* inst = Lookup( instance_id );
	if( !inst )
		return;

	const char* nav = url && url[0] ? url : "about:blank";
	Chrome_DPrintf( 1.f, "[chrome] Navigate id=%d -> %s\n", instance_id, nav );
	inst->LoadURL( std::string( nav ) );
}

void SendMouseMove( const int instance_id, const int x, const int y )
{
	(void)Lookup( instance_id );
	Chrome_DPrintf( 1.f, "[chrome] TODO SendMouseMove id=%d xy=(%d,%d)\n", instance_id, x, y );
}

void SendMouseButton( const int instance_id, const int button, const bool down )
{
	(void)Lookup( instance_id );
	Chrome_DPrintf(
		1.f, "[chrome] TODO SendMouseButton id=%d button=%d down=%d\n", instance_id, button, down ? 1 : 0 );
}

void SendMouseWheel( const int instance_id, const int delta_x, const int delta_y )
{
	(void)Lookup( instance_id );
	Chrome_DPrintf(
		1.f, "[chrome] TODO SendMouseWheel id=%d dxy=(%d,%d)\n", instance_id, delta_x, delta_y );
}

void SendKeyEvent( const int instance_id, const int key_code, const bool down )
{
	(void)Lookup( instance_id );
	Chrome_DPrintf(
		1.f, "[chrome] TODO SendKeyEvent id=%d key=%d down=%d\n", instance_id, key_code, down ? 1 : 0 );
}

unsigned int GetTextureId( const int instance_id )
{
	ChromeInstance* inst = Lookup( instance_id );
	if( !inst )
		return 0;
	return inst->texture_id();
}
} // namespace Chrome
