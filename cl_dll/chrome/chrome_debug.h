#pragma once

#if defined(__GNUC__)
#define CHROME_DEBUG_PRINTF_ATTR( fmt_idx, first_arg ) __attribute__( ( format( printf, fmt_idx, first_arg ) ) )
#else
#define CHROME_DEBUG_PRINTF_ATTR( fmt_idx, first_arg )
#endif

void ChromeDebug_Init();

float ChromeDebug_Level();

void ChromeDebug_DPrintf( float min_level, const char* fmt, ... ) CHROME_DEBUG_PRINTF_ATTR( 2, 3 );

void ChromeDebug_Msg( const char* fmt, ... ) CHROME_DEBUG_PRINTF_ATTR( 1, 2 );

#undef CHROME_DEBUG_PRINTF_ATTR
