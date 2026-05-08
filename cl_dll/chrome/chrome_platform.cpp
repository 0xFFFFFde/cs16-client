#if defined(CS16CLIENT_ENABLE_CEF)

#include "chrome_environment.h"
#include "chrome_platform.h"
#include "chrome_debug.h"

#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include "wrapper/cef_library_loader.h"
#endif

namespace iHTMLChrome {
namespace CEF {

#if defined(_WIN32)
namespace {
bool Utf8ToWide( const std::string& utf8, std::wstring& out )
{
	const int n = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0 );
	if( n <= 0 )
		return false;

	out.resize( static_cast<std::size_t>( n ) );
	if( MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, out.data(), n ) <= 0 )
		return false;
	return true;
}

bool LoadOptionalChromeElf( const std::filesystem::path& cef_dir_path )
{
	const std::filesystem::path elf_path = cef_dir_path / "chrome_elf.dll";
	if( !std::filesystem::exists( elf_path ) )
		return true;

	std::wstring elf_wide;
	if( !Utf8ToWide( elf_path.generic_string(), elf_wide ) )
		return false;

	HMODULE elf_module = LoadLibraryExW(
		elf_wide.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
	if( !elf_module )
	{
		Chrome_Printf( "[chrome] preload chrome_elf.dll failed (err=%lu)\n",
			static_cast<unsigned long>( GetLastError() ) );
		return false;
	}

	return true;
}
} // namespace
#endif

bool Platform_GetExecutablePath( std::string& executable_path )
{
#if defined(_WIN32)
	wchar_t wide[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW( nullptr, wide, sizeof( wide ) / sizeof( wide[0] ) );
	if( n == 0 )
		return false;

	const int bytes = WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
	if( bytes <= 1 )
		return false;

	executable_path.resize( static_cast<std::size_t>( bytes - 1 ) );
	WideCharToMultiByte( CP_UTF8, 0, wide, -1, executable_path.data(), bytes, nullptr, nullptr );
	return true;
#elif defined(__linux__)
	char buf[PATH_MAX];
	const ssize_t len = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );
	if( len <= 0 )
		return false;
	buf[len] = '\0';
	executable_path.assign( buf );
	return true;
#elif defined(__APPLE__)
	char buf[4096];
	uint32_t sz = sizeof( buf );
	if( _NSGetExecutablePath( buf, &sz ) != 0 )
		return false;
	executable_path.assign( buf );
	return true;
#else
	(void)executable_path;
	return false;
#endif
}

CefMainArgs Platform_CreateMainArgs()
{
#if defined(_WIN32)
	return CefMainArgs( GetModuleHandleW( nullptr ) );
#else
	static char arg0[] = "xash3d";
	static char* argv[] = { arg0, nullptr };
	return CefMainArgs( 1, argv );
#endif
}

bool Platform_LoadCEF( const CefEnvironment& env )
{
#if defined(_WIN32)
	const std::filesystem::path cef_dir_path( env.cefDir );
	if( !LoadOptionalChromeElf( cef_dir_path ) )
		return false;

	const std::filesystem::path libcef_path = cef_dir_path / "libcef.dll";
	std::wstring libcef_wide;
	if( !Utf8ToWide( libcef_path.generic_string(), libcef_wide ) )
		return false;

	HMODULE module = LoadLibraryExW(
		libcef_wide.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
	if( !module )
	{
		Chrome_Printf( "[chrome] LoadLibrary(libcef.dll) failed %s (err=%lu)\n",
			libcef_path.generic_string().c_str(),
			static_cast<unsigned long>( GetLastError() ) );
		return false;
	}

	Chrome_DPrintf( 1.f, "[chrome] loaded %s\n", libcef_path.generic_string().c_str() );
	return true;
#elif defined(__linux__)
	const std::filesystem::path libcef_path = std::filesystem::path( env.cefDir ) / "libcef.so";
	if( !dlopen( libcef_path.generic_string().c_str(), RTLD_NOW | RTLD_GLOBAL ) )
	{
		Chrome_Printf( "[chrome] dlopen failed %s: %s\n", libcef_path.generic_string().c_str(), dlerror() );
		return false;
	}

	Chrome_DPrintf( 1.f, "[chrome] loaded %s\n", libcef_path.generic_string().c_str() );
	return true;
#elif defined(__APPLE__)
	const std::filesystem::path framework_binary =
		std::filesystem::path( env.frameworkDir ) / "Chromium Embedded Framework";
	if( !cef_load_library( framework_binary.generic_string().c_str() ) )
	{
		Chrome_Printf( "[chrome] cef_load_library failed: %s\n", framework_binary.generic_string().c_str() );
		return false;
	}
	return true;
#else
	(void)env;
	return false;
#endif
}

} // namespace CEF
} // namespace iHTMLChrome

#endif
