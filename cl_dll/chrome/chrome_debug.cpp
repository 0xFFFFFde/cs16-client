#include "chrome_debug.h"

#include "hud.h"
#include "cl_util.h"

#include <cstdarg>
#include <cstdio>

namespace
{
cvar_t* cl_chrome_debug = nullptr;
}

void ChromeDebug_Init()
{
	if( cl_chrome_debug )
		return;

	cl_chrome_debug = CVAR_CREATE( "cl_chrome_debug", "0", FCVAR_ARCHIVE );
}

float ChromeDebug_Level()
{
	return cl_chrome_debug ? cl_chrome_debug->value : 0.f;
}

void ChromeDebug_DPrintf( float min_level, const char* fmt, ... )
{
	if( !cl_chrome_debug || cl_chrome_debug->value < min_level )
		return;

	char msg[2048];
	va_list args;
	va_start( args, fmt );
	vsnprintf( msg, sizeof( msg ), fmt, args );
	va_end( args );

	gEngfuncs.Con_DPrintf( "%s", msg );
}

void ChromeDebug_Msg( const char* fmt, ... )
{
	char msg[2048];
	va_list args;
	va_start( args, fmt );
	vsnprintf( msg, sizeof( msg ), fmt, args );
	va_end( args );

	gEngfuncs.Con_Printf( "%s", msg );
}
