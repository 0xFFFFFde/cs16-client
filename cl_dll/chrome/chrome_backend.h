#pragma once

#include <memory>
#include <string>

#include "chrome_types.h"

namespace iHTMLChrome {
namespace CEF {
class ChromeInstance;

class ChromeBackend
{
public:
	virtual ~ChromeBackend() = default;

	virtual bool Initialize( ChromeInstance& instance, const char* start_url ) = 0;
	virtual void Shutdown() = 0;
	virtual void LoadURL( const std::string& url ) = 0;
	virtual void Resize( int width, int height ) = 0;
	virtual void HandleInput( const InputEvent& event ) = 0;
	virtual void Tick() = 0;
};

std::unique_ptr<ChromeBackend> CreateChromeBackend();
// Page scale: 1.0 = 100% size, 0.8 ≈ 80%. Mapped to CEF SetZoomLevel (log scale).
void SetChromeZoom( double scale );
void SetChromeFrameRate( int frames_per_second );
} // namespace CEF
} // namespace iHTMLChrome
