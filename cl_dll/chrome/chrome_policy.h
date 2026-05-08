#pragma once

#if defined(CS16CLIENT_ENABLE_CEF)

#include "cef_base.h"
#include "cef_command_line.h"

namespace iHTMLChrome {
namespace CEF {

void ApplyCefCommandLinePolicy( CefRefPtr<CefCommandLine> command_line );

} // namespace CEF
} // namespace iHTMLChrome

#endif
