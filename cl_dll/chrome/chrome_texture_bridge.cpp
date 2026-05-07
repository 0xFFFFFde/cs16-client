#include "chrome_texture_bridge.h"

#include "wrect.h"
#include "cl_dll.h"

#include <vector>

namespace iHTMLChrome {
namespace CEF {
bool TextureBridge::Initialize()
{
	return true;
}

void TextureBridge::Shutdown()
{
	DestroyTexture();
}

void TextureBridge::EnsureSize( int width, int height )
{
	if( width <= 0 || height <= 0 )
		return;

	if( texture_id_ == 0 || width_ != width || height_ != height )
		CreateTexture( width, height );
}

void TextureBridge::Upload( const FrameBuffer& frame )
{
	if( frame.width <= 0 || frame.height <= 0 || frame.rgba.empty() )
		return;

	if( gRenderAPI.AVI_UploadRawFrame )
	{
		EnsureSize( frame.width, frame.height );
		if( texture_id_ == 0 )
			return;

		gRenderAPI.AVI_UploadRawFrame( texture_id_, frame.width, frame.height, frame.width, frame.height, frame.rgba.data() );
		return;
	}

	DestroyTexture();
	if( !gRenderAPI.GL_CreateTexture )
		return;

	const std::size_t pixels = static_cast<std::size_t>( frame.width ) * static_cast<std::size_t>( frame.height );
	std::vector<unsigned char> rgba;
	rgba.resize( pixels * 4 );
	const unsigned char* bgra = frame.rgba.data();
	for( std::size_t i = 0; i < pixels; ++i )
	{
		const std::size_t s = i * 4;
		const std::size_t d = i * 4;
		rgba[d + 0] = bgra[s + 2];
		rgba[d + 1] = bgra[s + 1];
		rgba[d + 2] = bgra[s + 0];
		rgba[d + 3] = bgra[s + 3];
	}

	texture_id_ = gRenderAPI.GL_CreateTexture(
		"*chrome_offscreen",
		frame.width,
		frame.height,
		rgba.data(),
		static_cast<texFlags_t>( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA | TF_NEAREST ) );
	width_ = frame.width;
	height_ = frame.height;
}

void TextureBridge::CreateTexture( int width, int height )
{
	DestroyTexture();

	if( !gRenderAPI.GL_CreateTexture )
		return;

	texture_id_ = gRenderAPI.GL_CreateTexture(
		"*chrome_offscreen",
		width,
		height,
		nullptr,
		static_cast<texFlags_t>( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA ) );
	width_ = width;
	height_ = height;
}

void TextureBridge::DestroyTexture()
{
	if( texture_id_ != 0 && gRenderAPI.GL_FreeTexture )
		gRenderAPI.GL_FreeTexture( texture_id_ );

	texture_id_ = 0;
	width_ = 0;
	height_ = 0;
}
} // namespace CEF
} // namespace iHTMLChrome
