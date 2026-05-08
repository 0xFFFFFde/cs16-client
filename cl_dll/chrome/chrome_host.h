#pragma once

using ChromeOnPaintCallback =
	void ( * )( void* userdata, const unsigned char* rgba, int width, int height );

namespace Chrome {

bool Initialize();
void Shutdown();
void RunFrame();
void PresentDebugPresentationOverlays();

int Create( const char* url, int width, int height, ChromeOnPaintCallback callback, void* userdata );
void Destroy( int instance_id );
void Resize( int instance_id, int width, int height );
void Navigate( int instance_id, const char* url );

void SendMouseMove( int instance_id, int x, int y );
void SendMouseButton( int instance_id, int button, bool down );
void SendMouseWheel( int instance_id, int delta_x, int delta_y );
void SendKeyEvent( int instance_id, int key_code, bool down );

unsigned int GetTextureId( int instance_id );
} // namespace Chrome
