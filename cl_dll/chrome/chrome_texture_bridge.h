#pragma once

#include "chrome_types.h"

namespace iHTMLChrome {
namespace CEF {
class TextureBridge
{
public:
	bool Initialize();
	void Shutdown();

	void EnsureSize( int width, int height );
	void Upload( const FrameBuffer& frame );

	unsigned int texture_id() const { return texture_id_; }
	int width() const { return width_; }
	int height() const { return height_; }

private:
	void CreateTexture( int width, int height );
	void DestroyTexture();

	unsigned int texture_id_ = 0;
	int width_ = 0;
	int height_ = 0;
};
} // namespace CEF
} // namespace iHTMLChrome
