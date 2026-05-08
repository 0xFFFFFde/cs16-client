#pragma once

#if defined(CS16CLIENT_ENABLE_CEF)

namespace iHTMLChrome {
namespace CEF {

struct CefEnvironment;

bool CefInstallation_Validate( const CefEnvironment& env );
bool CefInstallation_EnsureCachePath( const CefEnvironment& env );

} // namespace CEF
} // namespace iHTMLChrome

#endif
