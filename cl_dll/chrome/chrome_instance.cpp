#include "chrome_instance.h"

#include <algorithm>
#include <cstring>

namespace iHTMLChrome {
namespace CEF {
ChromeInstance::ChromeInstance( int id ) : id_( id )
{
}

ChromeInstance::~ChromeInstance()
{
	Shutdown();
}

bool ChromeInstance::Initialize( const char* start_url )
{
	if( initialized_.load() )
		return true;

	if( start_url && start_url[0] )
		url_ = start_url;

	backend_ = CreateChromeBackend();
	if( !backend_ || !backend_->Initialize( *this, start_url ) )
		return false;

	if( !texture_bridge_.Initialize() )
		return false;

	initialized_.store( true );
	return true;
}

void ChromeInstance::Shutdown()
{
	if( !initialized_.load() )
		return;

	if( backend_ )
	{
		backend_->Shutdown();
		backend_.reset();
	}

	texture_bridge_.Shutdown();
	initialized_.store( false );
}

void ChromeInstance::LoadURL( const std::string& url )
{
	url_ = url;
	if( backend_ )
		backend_->LoadURL( url );
}

void ChromeInstance::Resize( int width, int height )
{
	browser_width_ = std::max( 0, width );
	browser_height_ = std::max( 0, height );
	texture_bridge_.EnsureSize( browser_width_, browser_height_ );
	if( backend_ )
		backend_->Resize( browser_width_, browser_height_ );
}

void ChromeInstance::Tick()
{
	if( !initialized_.load() )
		return;

	if( backend_ )
	{
		std::queue<InputEvent> input_copy;
		{
			std::lock_guard<std::mutex> lock( input_mutex_ );
			input_copy.swap( pending_input_ );
		}

		while( !input_copy.empty() )
		{
			backend_->HandleInput( input_copy.front() );
			input_copy.pop();
		}

		backend_->Tick();
	}

	UploadLatestFrame();
}

void ChromeInstance::EnqueueInput( const InputEvent& event )
{
	std::lock_guard<std::mutex> lock( input_mutex_ );
	pending_input_.push( event );
}

void ChromeInstance::OnCefPaint( const std::uint8_t* rgba, int width, int height, std::uint64_t sequence )
{
	if( !rgba || width <= 0 || height <= 0 )
		return;

	FrameBuffer next;
	next.width = width;
	next.height = height;
	next.sequence = sequence;
	const std::size_t byte_count = static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ) * 4;
	next.rgba.resize( byte_count );
	std::memcpy( next.rgba.data(), rgba, byte_count );

	std::lock_guard<std::mutex> lock( frame_mutex_ );
	pending_frame_ = std::move( next );
	has_pending_frame_ = true;
}

void ChromeInstance::UploadLatestFrame()
{
	{
		std::lock_guard<std::mutex> lock( frame_mutex_ );
		if( !has_pending_frame_ )
			return;

		staged_frame_ = std::move( pending_frame_ );
		has_pending_frame_ = false;
	}

	texture_bridge_.Upload( staged_frame_ );
}
} // namespace CEF
} // namespace iHTMLChrome
