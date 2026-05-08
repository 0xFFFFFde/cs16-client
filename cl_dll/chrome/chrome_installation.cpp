#include "chrome_installation.h"

#if defined(CS16CLIENT_ENABLE_CEF)

#include "chrome_environment.h"
#include "chrome_debug.h"

#include <filesystem>

namespace iHTMLChrome {
namespace CEF {
namespace {

bool Exists( const std::filesystem::path& path )
{
	std::error_code ec;
	return std::filesystem::exists( path, ec );
}

bool HasAnyResourceBundle( const std::filesystem::path& resources_dir )
{
	return Exists( resources_dir / "icudtl.dat" ) || Exists( resources_dir / "chrome_100_percent.pak" );
}
} // namespace

bool CefInstallation_Validate( const CefEnvironment& env )
{
#if defined(_WIN32) || defined(__linux__)
	const std::filesystem::path cef_dir( env.cefDir );
	if( !Exists( cef_dir ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: missing folder %s\n", env.cefDir.c_str() );
		return false;
	}

	if( !HasAnyResourceBundle( std::filesystem::path( env.resourcesDir ) ) &&
		!HasAnyResourceBundle( cef_dir / "Resources" ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: %s lacks CEF resource bundles\n", env.cefDir.c_str() );
		return false;
	}

	return true;
#elif defined(__APPLE__)
	const std::filesystem::path framework_dir( env.frameworkDir );
	if( !Exists( framework_dir ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: missing framework bundle %s\n", env.frameworkDir.c_str() );
		return false;
	}

	if( !HasAnyResourceBundle( std::filesystem::path( env.resourcesDir ) ) )
	{
		Chrome_Printf( "[chrome] CefRuntime: framework resources missing in %s\n", env.resourcesDir.c_str() );
		return false;
	}

	return true;
#else
	(void)env;
	Chrome_Printf( "[chrome] CefRuntime: unsupported platform\n" );
	return false;
#endif
}

bool CefInstallation_EnsureCachePath( const CefEnvironment& env )
{
	if( env.cachePath.empty() )
		return false;

	std::error_code ec;
	std::filesystem::create_directories( std::filesystem::path( env.cachePath ), ec );
	if( ec )
	{
		Chrome_Printf( "[chrome] CefRuntime: cannot create cache dir %s (%s)\n",
			env.cachePath.c_str(), ec.message().c_str() );
		return false;
	}

	return true;
}

} // namespace CEF
} // namespace iHTMLChrome

#endif
