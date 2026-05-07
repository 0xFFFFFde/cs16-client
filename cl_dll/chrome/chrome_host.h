#pragma once

struct ChromeRect
{
	int x;
	int y;
	int width;
	int height;
};

void Chrome_Init();
void Chrome_Shutdown();

void Chrome_RunFrame();
void Chrome_Draw();

bool Chrome_HasActiveBrowser();
bool Chrome_WantsInputCapture();
bool Chrome_ProcessIN_MouseEvent( int mstate );
bool Chrome_HandleKeyEvent( int down, int keynum );

bool Chrome_Create( const char* url, const ChromeRect* viewport, int width, int height, int flags );
void Chrome_Destroy();
void Chrome_LoadURL( const char* url );
void Chrome_SetViewport( const ChromeRect* viewport );
void Chrome_SendMouseMove( int screen_x, int screen_y );
void Chrome_SendMouseButton( int down, int button, int screen_x, int screen_y );
void Chrome_SendMouseWheel( int delta, int screen_x, int screen_y );
void Chrome_SendKeyEvent( int down, int keynum );
void Chrome_SendCharEvent( unsigned int codepoint );

void Chrome_ToggleBrowser();
void Chrome_OpenUrlFromCommand();

void Chrome_MotdOpenURL( const char* url );
void Chrome_MotdOpenHtml( const char* html );
void Chrome_MotdShow( bool state );
bool Chrome_MotdIsVisible();
