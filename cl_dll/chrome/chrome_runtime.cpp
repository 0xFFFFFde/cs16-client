#if defined(CS16CLIENT_ENABLE_CEF)

#if defined(XASH_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "build.h"
#include "chrome_runtime.h"
#include "chrome_debug.h"

#if defined(XASH_WIN32)
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#elif defined(XASH_APPLE)
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(XASH_LINUX)
#include <dlfcn.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "cef_app.h"
#include "cef_command_line.h"

#if defined(XASH_APPLE)
#include "wrapper/cef_library_loader.h"
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

std::string ModCefDir( const std::string& exe_dir )
{
	return JoinPath( JoinPath( exe_dir, CS16CLIENT_GAME_MOD_DIR ), "cef" );
}

std::string GameModRoot( const std::string& exe_dir )
{
	return JoinPath( exe_dir, CS16CLIENT_GAME_MOD_DIR );
}

#if defined(XASH_APPLE)
std::string FindMacCefFrameworkBundle( const std::string& exe_dir )
{
	const std::string mod = GameModRoot( exe_dir );
	return JoinPath( mod, "Chromium Embedded Framework.framework" );
}
#endif

#if defined(XASH_WIN32)

const wchar_t* Utf8ToWideTemp( const char* utf8, std::wstring& storage )
{
	const int n = MultiByteToWideChar( CP_UTF8, 0, utf8, -1, nullptr, 0 );
	if( n <= 0 )
		return nullptr;
	storage.resize( static_cast<std::size_t>( n ) );
	MultiByteToWideChar( CP_UTF8, 0, utf8, -1, storage.data(), n );
	return storage.c_str();
}

	bool LoadBundledElf( const std::wstring& dir_wide )
	{
	std::wstring elf = dir_wide;
	if( !elf.empty() && elf.back() != L'\\' )
		elf += L'\\';
	elf += L"chrome_elf.dll";
	HMODULE elf_mod = LoadLibraryExW( elf.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR );
	if( !elf_mod )
		elf_mod = LoadLibraryW( elf.c_str() );
	if( !elf_mod )
	{
		if( Chrome_Level() >= 1.f )
			Chrome_Printf( "[chrome] preload chrome_elf.dll failed (err=%lu)\n",
				static_cast<unsigned long>( GetLastError() ) );
		return false;
	}
	return true;
}

bool LoadCefDLLsFromModDir( const std::string& cef_dir )
{
	std::wstring cef_wide;
	if( !Utf8ToWideTemp( cef_dir.c_str(), cef_wide ) )
		return false;

	if( !SetDllDirectoryW( cef_wide.c_str() ) )
	{
		Chrome_Printf( "[chrome] SetDllDirectoryW failed for %s (err=%lu)\n",
			cef_dir.c_str(), static_cast<unsigned long>( GetLastError() ) );
		return false;
	}

	if( PathExists( JoinPath( cef_dir, "chrome_elf.dll" ) ) && !LoadBundledElf( cef_wide ) )
		return false;

	const std::string libcef = JoinPath( cef_dir, "libcef.dll" );
	std::wstring lib_wide;
	if( !Utf8ToWideTemp( libcef.c_str(), lib_wide ) )
		return false;

	HMODULE mod = LoadLibraryExW(
		lib_wide.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
	if( !mod )
		mod = LoadLibraryW( lib_wide.c_str() );
	if( !mod )
	{
		Chrome_Printf( "[chrome] LoadLibrary(libcef.dll) failed %s (err=%lu)\n",
			libcef.c_str(), static_cast<unsigned long>( GetLastError() ) );
		return false;
	}
	Chrome_DPrintf( 1.f, "[chrome] loaded %s\n", libcef.c_str() );
	return true;
}

#elif defined(XASH_LINUX)

bool LoadCefDLLsFromModDir( const std::string& cef_dir )
{
	const std::string path = JoinPath( cef_dir, "libcef.so" );
	if( !dlopen( path.c_str(), RTLD_NOW | RTLD_GLOBAL ) )
	{
		Chrome_Printf( "[chrome] dlopen failed %s: %s\n", path.c_str(), dlerror() );
		return false;
	}
	Chrome_DPrintf( 1.f, "[chrome] loaded %s\n", path.c_str() );
	return true;
}

#else
bool LoadCefDLLsFromModDir( const std::string& )
{
	return false;
}
#endif

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
		command_line->AppendSwitch( "disable-video-capture-use-gpu-memory-buffer" );
		command_line->AppendSwitch( "enable-zero-copy" );
		command_line->AppendSwitch( "disable-partial-raster" );
		command_line->AppendSwitch( "js-flags=--max-old-space-size=64" );
		command_line->AppendSwitch( "disable-javascript-harmony-shipping" );
		command_line->AppendSwitch( "disable-plugins" );
		command_line->AppendSwitch( "disable-translate" );
		command_line->AppendSwitch( "metrics-recording-only" );
		command_line->AppendSwitch( "enable-low-end-device-mode" );
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

	Chrome_Init();

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
	settings.log_severity = Chrome_Level() >= 1.f ? LOGSEVERITY_VERBOSE : LOGSEVERITY_ERROR;

	const std::string exe = GetExecutablePathUtf8();
	if( exe.empty() )
	{
		Chrome_Printf( "[chrome] CefRuntime: executable path unresolved\n" );
		return false;
	}
	const std::string exe_dir = DirName( exe );

#if defined(XASH_WIN32) || defined(XASH_LINUX)

	const std::string mod_cef = ModCefDir( exe_dir );

	if( !PathExists( mod_cef ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: missing folder %s (copy CEF release into …/cstrike/cef/)\n",
			mod_cef.c_str() );
		return false;
	}

	if( !( PathExists( JoinPath( mod_cef, "icudtl.dat" ) ) ||
		    PathExists( JoinPath( mod_cef, "chrome_100_percent.pak" ) ) ) &&
	    !PathExists( JoinPath( mod_cef, "Resources/chrome_100_percent.pak" ) ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: %s lacks CEF resource bundles\n", mod_cef.c_str() );
		return false;
	}

	if( !LoadCefDLLsFromModDir( mod_cef ) )
		return false;

	std::string resources_dir = mod_cef;
	if( !PathExists( JoinPath( resources_dir, "icudtl.dat" ) ) &&
	    PathExists( JoinPath( mod_cef, "Resources/icudtl.dat" ) ) )
		resources_dir = JoinPath( mod_cef, "Resources" );

	const std::string locales_dir = FindLocalesDir( resources_dir );
	const std::string cache_root = JoinPath( exe_dir, "htmlcache" );
	if( !PathExists( cache_root ) )
	{
#if defined(XASH_WIN32)
		_mkdir( cache_root.c_str() );
#else
		::mkdir( cache_root.c_str(), 0755 );
#endif
	}

	CefString( &settings.browser_subprocess_path ) = exe;
	CefString( &settings.resources_dir_path ) = resources_dir;
	CefString( &settings.locales_dir_path ) = locales_dir;
	CefString( &settings.root_cache_path ) = cache_root;
	CefString( &settings.cache_path ) = "";
	settings.persist_session_cookies = false;

	Chrome_DPrintf( 1.f, "[chrome] resources_dir=%s\n", resources_dir.c_str() );

#elif defined(XASH_APPLE)

	const std::string framework_dir = FindMacCefFrameworkBundle( exe_dir );
	const std::string resources_dir = JoinPath( framework_dir, "Resources" );

	{
		const std::string framework_binary = JoinPath( framework_dir, "Chromium Embedded Framework" );
		if( !cef_load_library( framework_binary.c_str() ) )
		{
			Chrome_Printf( "[chrome] cef_load_library failed: %s\n", framework_binary.c_str() );
			return false;
		}
	}

	CefString( &settings.browser_subprocess_path ) = exe;
	CefString( &settings.framework_dir_path ) = framework_dir;
	CefString( &settings.resources_dir_path ) = resources_dir;
	// CefString( &settings.root_cache_path ) = "";
	// CefString( &settings.cache_path ) = "";
	settings.persist_session_cookies = false;

	Chrome_DPrintf( 1.f, "[chrome] framework=%s resources=%s\n", framework_dir.c_str(), resources_dir.c_str() );

#else
	Chrome_Printf( "[chrome] CefRuntime: unsupported platform\n" );
	return false;
#endif

	CefRefPtr<BrowserCefApp> app( new BrowserCefApp() );
		if( !CefInitialize( main_args, settings, app, nullptr ) )
	{
		Chrome_Printf( "[chrome] CefInitialize failed\n" );
#if defined(XASH_WIN32)
		SetDllDirectoryW( nullptr );
#endif
		return false;
	}

	initialized_ = true;
	Chrome_DPrintf( 1.f, "[chrome] CefRuntime initialized\n" );
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
	initialized_ = false;

#if defined(XASH_WIN32)
	SetDllDirectoryW( nullptr );
#endif

	Chrome_DPrintf( 1.f, "[chrome] CefRuntime shutdown complete\n" );
}
} // namespace CEF
} // namespace iHTMLChrome

#else // !CS16CLIENT_ENABLE_CEF

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
