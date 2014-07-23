#include "cinder/app/AppBasic.h"
#include "cinder/gl/Texture.h"
#include "cinder/params/Params.h"
#include "cinder/Text.h"

#include "boost/algorithm/string.hpp"

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "TcpClient.h"

class FtpClientApp : public ci::app::AppBasic 
{
public:
	void						draw();
	void						setup();
	void						update();
private:
	TcpClientRef				mClient;
	TcpSessionRef				mSession;
	std::string					mHost;
	
	size_t						mBytesRead;
	size_t						mContentLength;
	std::string					mFilename;
	int32_t						mIndex;
	HttpRequest					mHttpRequest;
	HttpResponse				mHttpResponse;
	
	void						write();
	
	void						onClose();
	void						onConnect( TcpSessionRef session );
	void						onError( std::string err, size_t bytesTransferred );
	void						onRead( ci::Buffer buffer );
	void						onResolve();
	void						onWrite( size_t bytesTransferred );
	
	ci::Font					mFont;
	std::vector<std::string>	mText;
	ci::gl::TextureRef			mTexture;

	float						mFrameRate;
	bool						mFullScreen;
	ci::params::InterfaceGlRef	mParams;
};

#include "cinder/Utilities.h"
#include <fstream>
#include <iostream>

using namespace ci;
using namespace ci::app;
using namespace std;

void FtpClientApp::draw()
{
	gl::clear( Colorf::black() );
	gl::setMatricesWindow( getWindowSize() );
	
	if ( mTexture ) {
		gl::draw( mTexture, Vec2i( 250, 20 ) );
	}
	
	mParams->draw();
}

void FtpClientApp::onClose()
{
	mText.push_back( "Disconnected" );
}

void FtpClientApp::onConnect( TcpSessionRef session )
{
	mHttpResponse	= HttpResponse();
	mSession		= session;
	mText.push_back( "Connected" );
	
	mSession->connectCloseEventHandler( &FtpClientApp::onClose, this );
	mSession->connectErrorEventHandler( &FtpClientApp::onError, this );
	mSession->connectReadEventHandler( &FtpClientApp::onRead, this );
	mSession->connectWriteEventHandler( &FtpClientApp::onWrite, this );
	
	console() << mHttpRequest << endl;
	
	mSession->write( mHttpRequest.toBuffer() );
}

void FtpClientApp::onError( string err, size_t bytesTransferred )
{
	string text = "Error";
	if ( !err.empty() ) {
		text += ": " + err;
	}
	mText.push_back( text );
}

void FtpClientApp::onRead( ci::Buffer buffer )
{
	size_t sz	= buffer.getDataSize();
	mBytesRead	+= sz;
	mText.push_back( toString( sz ) + " bytes read" );
	
	if ( !mHttpResponse.hasHeader() ) {
		
		// Parse header
		mHttpResponse.parseHeader( HttpResponse::bufferToString( buffer ) );
		buffer = HttpResponse::removeHeader( buffer );
		
		// Get content-length
		for ( const KeyValuePair& kvp : mHttpResponse.getHeaders() ) {
			if ( kvp.first == "Content-Length" ) {
				mContentLength = fromString<size_t>( kvp.second );
				break;
			}
		}
	}
	
	// Append buffer to body
	mHttpResponse.append( buffer );
	
	if ( mBytesRead < mContentLength ) {
		
		// Keep reading until we hit the content length
		mSession->read();
	} else {

		mText.push_back( "Read complete" );
		mText.push_back( toString( mHttpResponse.getStatusCode() ) + " " + mHttpResponse.getReason() );
		
		if ( mHttpResponse.getStatusCode() == 200 ) {
			for ( const KeyValuePair& kvp : mHttpResponse.getHeaders() ) {
				
				// Choose file extension based on MIME type
				if ( kvp.first == "Content-Type" ) {
					string mime = kvp.second;
					
					if ( mime == "audio/mp3" ) {
						mFilename += ".mp3";
					} else if ( mime == "image/jpeg" ) {
						mFilename += ".jpg";
					} else if ( mime == "image/png" ) {
						mFilename += ".png";
					}
				} else if ( kvp.first == "Connection" ) {
					
					// Close connection if requested by server
					if ( kvp.second == "close" ) {
						mSession->close();
					}
				}
			}

			// Save the file
			ofstream file;
			fs::path path = getAppPath();
#if !defined ( CINDER_MSW )
			path = path.parent_path();
#endif
			path = path / mFilename;
			file.open( path.string().c_str(), ios::out | ios::trunc | ios::binary );
			file.close();
			
			mText.push_back( mFilename + " downloaded" );
		} else {
			
			// Write error
			mText.push_back( "Response: " +  HttpResponse::bufferToString( mHttpResponse.getBody() ) );
			
			mSession->close();
		}
	}
}

void FtpClientApp::onResolve()
{
	mText.push_back( "Endpoint resolved" );
}

void FtpClientApp::onWrite( size_t bytesTransferred )
{
	mText.push_back(toString( bytesTransferred ) + " bytes written" );
	mSession->read();
}

void FtpClientApp::setup()
{
	gl::enable( GL_TEXTURE_2D );
	
	mBytesRead		= 0;
	mContentLength	= 0;
	mFont			= Font( "Georgia", 24 );
	mFrameRate		= 0.0f;
	mFullScreen		= false;
	mHost			= "127.0.0.1";
	mIndex			= 0;
	
	mHttpRequest = HttpRequest( "GET", "/", HttpVersion::HTTP_1_1 );
	mHttpRequest.setHeader( "Host",			mHost );
	mHttpRequest.setHeader( "Accept",		"*/*" );

	mParams = params::InterfaceGl::create( "Params", Vec2i( 200, 150 ) );
	mParams->addParam( "Frame rate",	&mFrameRate,			"", true );
	mParams->addParam( "Full screen",	&mFullScreen,			"key=f" );
	mParams->addParam( "Image index",	&mIndex,				"min=0 max=3 step=1 keyDecr=i keyIncr=I" );
	mParams->addParam( "Host",			&mHost );
	mParams->addButton( "Write",		[ & ]() { write(); },	"key=w" );
	mParams->addButton( "Quit",			[ & ]() { quit(); },	"key=q" );
	
	mClient = TcpClient::create( io_service() );
	mClient->connectConnectEventHandler( &FtpClientApp::onConnect, this );
	mClient->connectErrorEventHandler( &FtpClientApp::onError, this );
	mClient->connectResolveEventHandler( &FtpClientApp::onResolve, this );
}

void FtpClientApp::update()
{
	mFrameRate = getFrameRate();
	
	if ( mFullScreen != isFullScreen() ) {
		setFullScreen( mFullScreen );
		mFullScreen = isFullScreen();
	}

	if ( !mText.empty() ) {
		TextBox tbox = TextBox()
			.alignment( TextBox::LEFT )
			.font( mFont )
			.size( Vec2i( getWindowWidth() - 250, TextBox::GROW ) )
			.text( "" );
		for ( vector<string>::const_reverse_iterator iter = mText.rbegin(); iter != mText.rend(); ++iter ) {
			tbox.appendText( "> " + *iter + "\n" );
		}
		tbox.setColor( ColorAf( 1.0f, 0.8f, 0.75f, 1.0f ) );
		tbox.setBackgroundColor( ColorAf::black() );
		tbox.setPremultiplied( false );
		mTexture = gl::Texture::create( tbox.render() );
		while ( mText.size() > 75 ) {
			mText.erase( mText.begin() );
		}
	}
}

void FtpClientApp::write()
{
	if ( mSession && mSession->getSocket()->is_open() ) {
		return;
	}
	
	// Reset download stats
	mBytesRead		= 0;
	mContentLength	= 0;
	
	// Update request body
	string index	= toString( mIndex );
	mFilename		= index;
	Buffer body		= HttpRequest::stringToBuffer( index );
	mHttpRequest.setBody( body );
	
	mText.push_back( "Connecting to:\n" + mHost + ":2000" );
	
	// Ports <1024 are restricted to root
	mClient->connect( mHost, 2000 );
}

CINDER_APP_BASIC( FtpClientApp, RendererGl )