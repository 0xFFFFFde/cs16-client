#if defined(CS16CLIENT_ENABLE_CEF)

// =========================
// 1. PLATFORM DEFINES (FIRST)
// =========================
#if defined(XASH_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

// =========================
// 2. STL (safe now)
// =========================
#include <cstdio>
#include <cstring>
#include <string>

// =========================
// 3. ENGINE HEADERS
// =========================
#include "build.h"
#include "chrome_runtime.h"
#include "chrome_debug.h"

// =========================
// 4. PLATFORM SYSTEM HEADERS
// =========================
#if defined(XASH_WIN32)
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#elif defined(XASH_APPLE)
#include <mach-o/dyld.h>
#include <sys/stat.h>
#elif defined(XASH_LINUX)
#include <dlfcn.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// =========================
// 5. CEF (ALWAYS LAST)
// =========================
#include "cef_app.h"
#include "cef_command_line.h"

#if defined(XASH_APPLE)
#include "wrapper/cef_library_loader.h"
#endif

#if !defined(CS16CLIENT_CEF_DIST_PATH)
#define CS16CLIENT_CEF_DIST_PATH ""
#endif
#if !defined(CS16CLIENT_GAME_MOD_DIR)
#define CS16CLIENT_GAME_MOD_DIR "cstrike"
#endif

namespace
{
bool PathExists( const std::string& path )
{
	struct stat st;
	return ::stat( path.c_str(), &st ) == 0;
}

std::string DirName( const std::string& path )
{
	const std::size_t p = path.find_last_of( "/\\" );
	if( p == std::string::npos )
		return std::string();
	return path.substr( 0, p );
}

#if defined(XASH_WIN32)
std::string GetExecutablePathUtf8()
{
	wchar_t wide[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW( nullptr, wide, sizeof( wide ) / sizeof( wide[0] ) );
	if( n == 0 )
		return {};
	const int bytes = WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
	if( bytes <= 1 )
		return {};
	std::string out( static_cast<std::size_t>( bytes - 1 ), '\0' );
	WideCharToMultiByte( CP_UTF8, 0, wide, -1, out.data(), bytes, nullptr, nullptr );
	return out;
}
#elif defined(XASH_APPLE)
std::string GetExecutablePathUtf8()
{
	char buf[4096];
	uint32_t sz = sizeof( buf );
	if( _NSGetExecutablePath( buf, &sz ) != 0 )
		return {};
	return std::string( buf );
}
#elif defined(XASH_LINUX)
std::string GetExecutablePathUtf8()
{
	char buf[PATH_MAX];
	const ssize_t len = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );
	if( len <= 0 )
		return {};
	buf[len] = '\0';
	return std::string( buf );
}
#else
std::string GetExecutablePathUtf8()
{
	return {};
}
#endif

std::string JoinPath( const std::string& a, const std::string& b )
{
	if( a.empty() )
		return b;
	if( !b.empty() && ( a.back() == '/' || a.back() == '\\' ) )
		return a + b;
	return a + "/" + b;
}

std::string DistRoot()
{
	return std::string( CS16CLIENT_CEF_DIST_PATH );
}

#if defined(XASH_WIN32)
const char kLibCefName[] = "libcef.dll";
#elif defined(XASH_LINUX)
const char kLibCefName[] = "libcef.so";
#else
const char kLibCefName[] = "";
#endif

#if defined(XASH_LINUX)
bool CefLoadLibraryPath( const char* path )
{
	if( !dlopen( path, RTLD_NOW | RTLD_GLOBAL ) )
	{
		ChromeDebug_Msg( "[chrome] dlopen failed %s: %s\n", path, dlerror() );
		return false;
	}
	return true;
}
#elif defined(XASH_WIN32)
bool CefLoadLibraryPath( const char* path_utf8 )
{
	const int n = MultiByteToWideChar( CP_UTF8, 0, path_utf8, -1, nullptr, 0 );
	if( n <= 0 )
	{
		ChromeDebug_Msg( "[chrome] UTF-8 path failed for %s\n", path_utf8 );
		return false;
	}
	std::wstring wide( static_cast<std::size_t>( n ), L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, path_utf8, -1, wide.data(), n );
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif
	HMODULE mod = LoadLibraryExW( wide.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
	if( !mod )
		mod = LoadLibraryW( wide.c_str() );
	if( !mod )
	{
		ChromeDebug_Msg( "[chrome] LoadLibrary failed %s (err=%lu)\n", path_utf8,
			static_cast<unsigned long>( GetLastError() ) );
		return false;
	}
	return true;
}
#endif

#if defined( XASH_WIN32 )
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif

static bool Win32PreloadChromeElfFromDir( const std::string& dir )
{
	const std::string elf = JoinPath( dir, "chrome_elf.dll" );
	if( !PathExists( elf ) )
		return true;
	const int n = MultiByteToWideChar( CP_UTF8, 0, elf.c_str(), -1, nullptr, 0 );
	if( n <= 0 )
	{
		ChromeDebug_Msg( "[chrome] UTF-8 path failed for %s\n", elf.c_str() );
		return false;
	}
	std::wstring wide( static_cast<std::size_t>( n ), L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, elf.c_str(), -1, wide.data(), n );
	HMODULE mod = LoadLibraryExW( wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR );
	if( !mod )
		mod = LoadLibraryW( wide.c_str() );
	if( !mod )
	{
		ChromeDebug_Msg( "[chrome] preload chrome_elf.dll failed %s (err=%lu)\n", elf.c_str(),
			static_cast<unsigned long>( GetLastError() ) );
		return false;
	}
	return true;
}
#endif

#if !defined(XASH_APPLE)
bool TryLoadLibCefFromDir( const std::string& dir, std::string* loaded_dir_out )
{
	if( dir.empty() )
		return false;
	const std::string path = JoinPath( dir, kLibCefName );
	if( !PathExists( path ) )
		return false;
#if defined( _WIN32 )
	if( !Win32PreloadChromeElfFromDir( dir ) )
		return false;
#endif
#if defined(XASH_LINUX) || defined(XASH_WIN32)
	if( !CefLoadLibraryPath( path.c_str() ) )
		return false;
	ChromeDebug_DPrintf( 1.f, "[chrome] loaded %s\n", path.c_str() );
	if( loaded_dir_out )
		*loaded_dir_out = dir;
	return true;
#else
	return false;
#endif
}
#endif

std::string FindResourcesDir( const std::string& dist, const std::string& exe_dir )
{
	const std::string game_mod = std::string( CS16CLIENT_GAME_MOD_DIR );
	const std::string bases[] = {
		JoinPath( JoinPath( exe_dir, game_mod ), "cef" ),
		JoinPath( exe_dir, game_mod ),
		JoinPath( JoinPath( exe_dir, game_mod ), "Resources" ),
		dist,
		exe_dir,
		JoinPath( exe_dir, "cef" ),
		JoinPath( exe_dir, ".." ),
		JoinPath( dist, "Release" ),
		JoinPath( dist, "Resources" ) };
	for( const std::string& base : bases )
	{
		if( base.empty() )
			continue;
		if( PathExists( JoinPath( base, "icudtl.dat" ) ) || PathExists( JoinPath( base, "chrome_100_percent.pak" ) ) )
			return base;
		const std::string try_resources = JoinPath( base, "Resources" );
		const std::string try_release = JoinPath( base, "Release" );
		if( PathExists( JoinPath( try_resources, "icudtl.dat" ) ) || PathExists( JoinPath( try_resources, "chrome_100_percent.pak" ) ) )
			return try_resources;
		if( PathExists( JoinPath( try_release, "icudtl.dat" ) ) || PathExists( JoinPath( try_release, "chrome_100_percent.pak" ) ) )
			return try_release;
	}
	for( const std::string& base : bases )
	{
		if( base.empty() )
			continue;
		const std::string r = JoinPath( base, "Resources" );
		if( PathExists( r ) )
			return r;
		const std::string rel = JoinPath( base, "Release" );
		if( PathExists( rel ) )
			return rel;
	}
	return exe_dir;
}

std::string ResourcesDirNextToLibCefOrSearch( const std::string& libcef_dir, const std::string& dist, const std::string& exe_dir )
{
	if( !libcef_dir.empty() && ( PathExists( JoinPath( libcef_dir, "icudtl.dat" ) ) ||
					    PathExists( JoinPath( libcef_dir, "chrome_100_percent.pak" ) ) ) )
		return libcef_dir;
	return FindResourcesDir( dist, exe_dir );
}

std::string FindLocalesDir( const std::string& resources_dir )
{
	const std::string loc = JoinPath( resources_dir, "locales" );
	if( PathExists( loc ) )
		return loc;
	return resources_dir;
}

class BrowserCefApp final : public CefApp, public CefBrowserProcessHandler
{
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing( const CefString&, CefRefPtr<CefCommandLine> command_line ) override
	{
		if( !command_line )
			return;

		command_line->AppendSwitch( "disable-gpu" );
		command_line->AppendSwitch( "disable-gpu-compositing" );
		command_line->AppendSwitch( "disable-gpu-vsync" );
		command_line->AppendSwitch( "disable-gpu-shader-disk-cache" );
		command_line->AppendSwitch( "disable-surfaces" );
		command_line->AppendSwitch( "disable-software-rasterizer" );
		command_line->AppendSwitch( "enable-begin-frame-scheduling" );
		command_line->AppendSwitch( "disable-media-stream" );
		command_line->AppendSwitch( "disable-webrtc" );
		command_line->AppendSwitch( "disable-speech-api" );
		command_line->AppendSwitch( "disable-extensions" );
		command_line->AppendSwitch( "disable-sync" );
		command_line->AppendSwitch( "disable-background-networking" );
		command_line->AppendSwitchWithValue( "autoplay-policy", "user-gesture-required" );
		// command_line->AppendSwitchWithValue( "enable-features", "RunVideoCaptureServiceInBrowserProcess" );
		command_line->AppendSwitch( "disable-video-capture-use-gpu-memory-buffer" );
		// command_line->AppendSwitchWithValue( "disable-features",
		// 	"MediaStream,MediaRouter,WebRtcAllowInputVolumeAdjustment,WebRtcHideLocalIpsWithMdns,"
		// 	"HardwareMediaKeyHandling,DialMediaRouteProvider,GlobalMediaControls,MediaSessionService" );

		// TEST
		command_line->AppendSwitch("enable-zero-copy");
		command_line->AppendSwitch("disable-partial-raster");
		command_line->AppendSwitch("js-flags=--max-old-space-size=64");
		command_line->AppendSwitch("disable-javascript-harmony-shipping");
		command_line->AppendSwitch("disable-plugins");
		command_line->AppendSwitch("disable-translate");
		command_line->AppendSwitch("metrics-recording-only");
		command_line->AppendSwitch("enable-low-end-device-mode");
		
		command_line->AppendSwitch( "single-process" );
	}

	IMPLEMENT_REFCOUNTING( BrowserCefApp );
};
} // namespace

namespace iHTMLChrome {
namespace CEF {

CefRuntime& CefRuntime::Get()
{
	static CefRuntime runtime;
	return runtime;
}

bool CefRuntime::Initialize()
{
	if( initialized_ )
		return true;

	ChromeDebug_Init();

#if defined(XASH_WIN32)
	CefMainArgs main_args( GetModuleHandleW( nullptr ) );
#else
	char arg0[] = "xash3d";
	char* argv[] = { arg0, nullptr };
	CefMainArgs main_args( 1, argv );
#endif

	CefSettings settings;
	settings.no_sandbox = true;
	settings.windowless_rendering_enabled = true;
	settings.external_message_pump = false;
	settings.multi_threaded_message_loop = false;
	settings.log_severity = LOGSEVERITY_ERROR;

#if defined(XASH_APPLE)
	char exe_path[4096];
	uint32_t exe_size = sizeof( exe_path );
	if( _NSGetExecutablePath( exe_path, &exe_size ) != 0 )
	{
		ChromeDebug_Msg( "[chrome] CEF init: _NSGetExecutablePath failed (size=%u)\n", exe_size );
		return false;
	}
	{
		const std::string exe( exe_path );
		const std::size_t slash_pos = exe.find_last_of( '/' );
		if( slash_pos == std::string::npos )
			return false;
		const std::string game_root = exe.substr( 0, slash_pos );
		const std::string framework_dir = game_root + "/CEF.framework";
		const std::string framework_binary = framework_dir + "/Chromium Embedded Framework";
		const std::string resources_dir = framework_dir + "/Resources";
		// const std::string cache_root = game_root + "/htmlcache";
		const std::string cache_root = "";


		if( !PathExists( framework_binary ) )
		{
			ChromeDebug_Msg( "[chrome] CEF init: framework binary missing: %s\n", framework_binary.c_str() );
			return false;
		}

		if( !cef_load_library( framework_binary.c_str() ) )
		{
			ChromeDebug_Msg( "[chrome] CEF init: cef_load_library failed: %s\n", framework_binary.c_str() );
			return false;
		}

		CefString( &settings.browser_subprocess_path ) = exe;
		CefString( &settings.framework_dir_path ) = framework_dir;
		CefString( &settings.resources_dir_path ) = resources_dir;
		if( !PathExists( cache_root ) )
			mkdir( cache_root.c_str(), 0755 );
		CefString( &settings.root_cache_path ) = cache_root;
		CefString( &settings.cache_path ) = "";
		settings.persist_session_cookies = false;
	}
#elif defined(XASH_WIN32) || defined(XASH_LINUX)
	{
		const std::string exe = GetExecutablePathUtf8();
		if( exe.empty() )
		{
			ChromeDebug_Msg( "[chrome] CEF init: could not resolve executable path\n" );
			return false;
		}
		const std::string exe_dir = DirName( exe );
		const std::string dist = DistRoot();
		const std::string game_mod = std::string( CS16CLIENT_GAME_MOD_DIR );
		const std::string mod_cef_dir = JoinPath( JoinPath( exe_dir, game_mod ), "cef" );

		std::string loaded_libcef_dir;
		bool loaded = false;
		loaded = TryLoadLibCefFromDir( mod_cef_dir, &loaded_libcef_dir );
		if( !loaded && !dist.empty() )
		{
			loaded = TryLoadLibCefFromDir( JoinPath( dist, "Release" ), &loaded_libcef_dir );
			if( !loaded )
				loaded = TryLoadLibCefFromDir( dist, &loaded_libcef_dir );
		}
		if( !loaded )
			loaded = TryLoadLibCefFromDir( JoinPath( exe_dir, "cef" ), &loaded_libcef_dir );
		if( !loaded )
			loaded = TryLoadLibCefFromDir( JoinPath( exe_dir, "Release" ), &loaded_libcef_dir );
		if( !loaded )
			loaded = TryLoadLibCefFromDir( exe_dir, &loaded_libcef_dir );
		if( !loaded )
		{
			ChromeDebug_Msg( "[chrome] CEF init: libcef not found (cmake -DCEF=…; copy CEF Release/ next to game exe)\n" );
			return false;
		}

		const std::string resources_dir = ResourcesDirNextToLibCefOrSearch( loaded_libcef_dir, dist, exe_dir );
		const std::string locales_dir = FindLocalesDir( resources_dir );
		const std::string cache_root = JoinPath( exe_dir, "htmlcache" );
		if( !PathExists( cache_root ) )
		{
#if defined(XASH_WIN32)
			_mkdir( cache_root.c_str() );
#else
			mkdir( cache_root.c_str(), 0755 );
#endif
		}

		CefString( &settings.browser_subprocess_path ) = exe;
		CefString( &settings.resources_dir_path ) = resources_dir;
		CefString( &settings.locales_dir_path ) = locales_dir;
		CefString( &settings.root_cache_path ) = cache_root;
		CefString( &settings.cache_path ) = "";
		settings.persist_session_cookies = false;

		ChromeDebug_DPrintf( 1.f, "[chrome] CEF resources_dir=%s\n", resources_dir.c_str() );
		ChromeDebug_DPrintf( 1.f, "[chrome] CEF locales_dir=%s\n", locales_dir.c_str() );
	}
#else
	ChromeDebug_Msg( "[chrome] CEF init: unsupported platform\n" );
	return false;
#endif

	CefRefPtr<BrowserCefApp> app( new BrowserCefApp() );
	if( !CefInitialize( main_args, settings, app, nullptr ) )
	{
		ChromeDebug_Msg( "[chrome] CefInitialize failed\n" );
		return false;
	}
	initialized_ = true;
	return true;
}

void CefRuntime::DoMessageLoopWork()
{
	if( initialized_ )
		CefDoMessageLoopWork();
}

void CefRuntime::Shutdown()
{
	if( !initialized_ )
		return;

	CefShutdown();
#if defined(XASH_APPLE)
	cef_unload_library();
#endif
	initialized_ = false;
}
} // namespace CEF
} // namespace iHTMLChrome

#else // !CEF

namespace iHTMLChrome {
namespace CEF {

CefRuntime& CefRuntime::Get()
{
	static CefRuntime runtime;
	return runtime;
}

bool CefRuntime::Initialize()
{
	initialized_ = true;
	return true;
}

void CefRuntime::DoMessageLoopWork()
{
}

void CefRuntime::Shutdown()
{
	initialized_ = false;
}
} // namespace CEF
} // namespace iHTMLChrome

#endif
