
/* Copyright (c) 2005-2009, Stefan Eilemann <eile@equalizergraphics.com>
                          , Maxim Makhinya
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "aglWindow.h"

#include "aglEventHandler.h"
#include "aglPipe.h"
#include "aglWindowEvent.h"
#include "global.h"
#include "pipe.h"
#include "window.h"

#define ERROR( code )                                               \
    std::string( reinterpret_cast< const char* >( aglErrorString( code )))

namespace eq
{

bool AGLWindowIF::processEvent( const AGLWindowEvent& event )
{
    EQASSERT( _window );
    return _window->processEvent( event );
}

AGLWindow::AGLWindow( Window* parent )
    : AGLWindowIF( parent )
    , _aglContext( 0 )
    , _carbonWindow( 0 )
    , _aglPBuffer( 0 )
    , _eventHandler( 0 )
{
}

AGLWindow::~AGLWindow( )
{
}

void AGLWindow::configExit( )
{
    WindowRef window = getCarbonWindow();
    setCarbonWindow( 0 );

    AGLPbuffer pbuffer = getAGLPBuffer();
    setAGLPBuffer( 0 );
    
    AGLContext context = getAGLContext();

    if( window )
    {
        Global::enterCarbon();
        if( getIAttribute( Window::IATTR_HINT_FULLSCREEN ) != ON )
        {
#ifdef LEOPARD
            aglSetWindowRef( context, 0 );
#else
            aglSetDrawable( context, 0 );
#endif
        }
        DisposeWindow( window );
        Global::leaveCarbon();
    }
    if( pbuffer )
        aglDestroyPBuffer( pbuffer );

    configExitFBO();
    exitGLEW();
    
    if( context )
    {
        Global::enterCarbon();
        aglSetCurrentContext( 0 );
        aglDestroyContext( context );
        Global::leaveCarbon();
        setAGLContext( 0 );
    }
    
    EQINFO << "Destroyed AGL window and context" << std::endl;
}

void AGLWindow::makeCurrent() const
{
    aglSetCurrentContext( _aglContext );

    AGLWindowIF::makeCurrent();
}

void AGLWindow::swapBuffers()
{
    aglSwapBuffers( _aglContext );
}

void AGLWindow::joinNVSwapBarrier( const uint32_t group, const uint32_t barrier)
{
    EQWARN << "NV_swap_group not supported on AGL" << std::endl;
}

bool AGLWindow::processEvent( const AGLWindowEvent& event )
{
    if( event.type == Event::WINDOW_RESIZE && _aglContext )
        aglUpdateContext( _aglContext );

    return AGLWindowIF::processEvent( event );
}

void AGLWindow::setAGLContext( AGLContext context )
{
    _aglContext = context;
}

//---------------------------------------------------------------------------
// AGL init
//---------------------------------------------------------------------------

bool AGLWindow::configInit( )
{
    AGLPixelFormat pixelFormat = chooseAGLPixelFormat();
    if( !pixelFormat )
        return false;

    AGLContext context = createAGLContext( pixelFormat );
    destroyAGLPixelFormat ( pixelFormat );
    setAGLContext( context );
    
    if( !context )
        return false;

    EQ_GL_CALL( makeCurrent( ));
    initGLEW();
    return configInitAGLDrawable();
}

AGLPixelFormat AGLWindow::chooseAGLPixelFormat()
{
    Pipe*    pipe    = getPipe();
    EQASSERT( pipe );
    EQASSERT( pipe->getOSPipe( ));
    EQASSERT( dynamic_cast< const AGLPipe* >( pipe->getOSPipe( )));

    const AGLPipe* osPipe = static_cast< const AGLPipe* >( pipe->getOSPipe( ));

    CGDirectDisplayID displayID = osPipe->getCGDisplayID();

    Global::enterCarbon();

#ifdef LEOPARD
    CGOpenGLDisplayMask glDisplayMask =
        CGDisplayIDToOpenGLDisplayMask( displayID );
#else
    GDHandle          displayHandle = 0;

    DMGetGDeviceByDisplayID( (DisplayIDType)displayID, &displayHandle, false );

    if( !displayHandle )
    {
        _window->setErrorMessage( "Can't get display handle" );
        Global::leaveCarbon();
        return 0;
    }
#endif

    // build attribute list
    std::vector<GLint> attributes;

    attributes.push_back( AGL_RGBA );
    attributes.push_back( GL_TRUE );
    attributes.push_back( AGL_ACCELERATED );
    attributes.push_back( GL_TRUE );

    if( getIAttribute( Window::IATTR_HINT_FULLSCREEN ) == ON && 
        getIAttribute( Window::IATTR_HINT_DRAWABLE )   == WINDOW )

        attributes.push_back( AGL_FULLSCREEN );

#ifdef LEOPARD
    attributes.push_back( AGL_DISPLAY_MASK );
    attributes.push_back( glDisplayMask );
#endif

    const int colorSize = getIAttribute( Window::IATTR_PLANES_COLOR );
    if( colorSize > 0 || colorSize == AUTO )
    {
        const GLint size = colorSize > 0 ? colorSize : 8;

        attributes.push_back( AGL_RED_SIZE );
        attributes.push_back( size );
        attributes.push_back( AGL_GREEN_SIZE );
        attributes.push_back( size );
        attributes.push_back( AGL_BLUE_SIZE );
        attributes.push_back( size );
    }
    const int alphaSize = getIAttribute( Window::IATTR_PLANES_ALPHA );
    if( alphaSize > 0 || alphaSize == AUTO )
    {
        attributes.push_back( AGL_ALPHA_SIZE );
        attributes.push_back( alphaSize>0 ? alphaSize : 8  );
    }
    const int depthSize = getIAttribute( Window::IATTR_PLANES_DEPTH );
    if( depthSize > 0 || depthSize == AUTO )
    { 
        attributes.push_back( AGL_DEPTH_SIZE );
        attributes.push_back( depthSize>0 ? depthSize : 24 );
    }
    const int stencilSize = getIAttribute( Window::IATTR_PLANES_STENCIL );
    if( stencilSize > 0 || stencilSize == AUTO )
    {
        attributes.push_back( AGL_STENCIL_SIZE );
        attributes.push_back( stencilSize>0 ? stencilSize : 1 );
    }
    const int accumSize  = getIAttribute( Window::IATTR_PLANES_ACCUM );
    const int accumAlpha = getIAttribute( Window::IATTR_PLANES_ACCUM_ALPHA );
    if( accumSize >= 0 )
    {
        attributes.push_back( AGL_ACCUM_RED_SIZE );
        attributes.push_back( accumSize );
        attributes.push_back( AGL_ACCUM_GREEN_SIZE );
        attributes.push_back( accumSize );
        attributes.push_back( AGL_ACCUM_BLUE_SIZE );
        attributes.push_back( accumSize );
        attributes.push_back( AGL_ACCUM_ALPHA_SIZE );
        attributes.push_back( accumAlpha >= 0 ? accumAlpha : accumSize );
    }
    else if( accumAlpha >= 0 )
    {
        attributes.push_back( AGL_ACCUM_ALPHA_SIZE );
        attributes.push_back( accumAlpha );
    }

    const int samplesSize  = getIAttribute( Window::IATTR_PLANES_SAMPLES );
    if( samplesSize >= 0 )
    {
        attributes.push_back( AGL_SAMPLE_BUFFERS_ARB );
        attributes.push_back( 1 );
        attributes.push_back( AGL_SAMPLES_ARB );
        attributes.push_back( samplesSize );
    }

    if( getIAttribute( Window::IATTR_HINT_DOUBLEBUFFER ) == ON ||
        ( getIAttribute( Window::IATTR_HINT_DOUBLEBUFFER ) == AUTO && 
          getIAttribute( Window::IATTR_HINT_DRAWABLE )     == WINDOW ))
    {
        attributes.push_back( AGL_DOUBLEBUFFER );
        attributes.push_back( GL_TRUE );
    }
    if( getIAttribute( Window::IATTR_HINT_STEREO ) == ON )
    {
        attributes.push_back( AGL_STEREO );
        attributes.push_back( GL_TRUE );
    }

    attributes.push_back( AGL_NONE );

    // build backoff list, least important attribute last
    std::vector<int> backoffAttributes;
    if( getIAttribute( Window::IATTR_HINT_DOUBLEBUFFER ) == AUTO &&
        getIAttribute( Window::IATTR_HINT_DRAWABLE )     == WINDOW  )

        backoffAttributes.push_back( AGL_DOUBLEBUFFER );

    if( stencilSize == AUTO )
        backoffAttributes.push_back( AGL_STENCIL_SIZE );

    // choose pixel format
    AGLPixelFormat pixelFormat = 0;
    while( true )
    {
#ifdef LEOPARD
        pixelFormat = aglCreatePixelFormat( &attributes.front( ));
#else
        pixelFormat = aglChoosePixelFormat( &displayHandle, 1,
                                            &attributes.front( ));
#endif

        if( pixelFormat ||              // found one or
            backoffAttributes.empty( )) // nothing else to try

            break;

        // Gradually remove backoff attributes
        const GLint attribute = backoffAttributes.back();
        backoffAttributes.pop_back();

        std::vector<GLint>::iterator iter = find( attributes.begin(), 
                                             attributes.end(), attribute );
        EQASSERT( iter != attributes.end( ));

        attributes.erase( iter, iter+2 ); // remove two item (attr, value)
    }

    if( !pixelFormat )
        _window->setErrorMessage( "Could not find a matching pixel format" );

    Global::leaveCarbon();
    return pixelFormat;
}

void AGLWindow::destroyAGLPixelFormat( AGLPixelFormat pixelFormat )
{
    if( !pixelFormat )
        return;

    Global::enterCarbon();
    aglDestroyPixelFormat( pixelFormat );
    Global::leaveCarbon();
}

AGLContext AGLWindow::createAGLContext( AGLPixelFormat pixelFormat )
{
    if( !pixelFormat )
    {
        _window->setErrorMessage( "No pixel format given" );
        return 0;
    }

    AGLContext    shareCtx    = 0;
    const Window* shareWindow = _window->getSharedContextWindow();
    const OSWindow* shareOSWindow = shareWindow ? shareWindow->getOSWindow() :0;
    if( shareOSWindow )
    {

        EQASSERT( dynamic_cast< const AGLWindow* >( shareOSWindow ));
        const AGLWindow* shareAGLWindow = static_cast< const AGLWindow* >(
                                              shareOSWindow );
        shareCtx = shareAGLWindow->getAGLContext();
    }
 
    Global::enterCarbon();
    AGLContext context = aglCreateContext( pixelFormat, shareCtx );

    if( !context ) 
    {
        _window->setErrorMessage( "Could not create AGL context: " + 
                                  ERROR( aglGetError( )));
        Global::leaveCarbon();
        return 0;
    }

    // set vsync on/off
    if( getIAttribute( Window::IATTR_HINT_SWAPSYNC ) != AUTO )
    {
        const GLint vsync = 
            ( getIAttribute( Window::IATTR_HINT_SWAPSYNC )==OFF ? 0 : 1 );
        aglSetInteger( context, AGL_SWAP_INTERVAL, &vsync );
    }

    aglSetCurrentContext( context );

    Global::leaveCarbon();

    EQINFO << "Created AGL context " << context << " shared with " << shareCtx
           << std::endl;
    return context;
}


bool AGLWindow::configInitAGLDrawable()
{
    switch( getIAttribute( Window::IATTR_HINT_DRAWABLE ))
    {
        case PBUFFER:
            return configInitAGLPBuffer();

        case FBO:
            return configInitFBO();

        default:
            EQWARN << "Unknown drawable type "
                   << getIAttribute( Window::IATTR_HINT_DRAWABLE )
                   << ", using window" << std::endl;
            // no break;
        case UNDEFINED:
        case WINDOW:
            if( getIAttribute( Window::IATTR_HINT_FULLSCREEN ) == ON )
                return configInitAGLFullscreen();
            else
                return configInitAGLWindow();
    }
}

bool AGLWindow::configInitAGLPBuffer()
{
    AGLContext context = getAGLContext();
    if( !context )
    {
        _window->setErrorMessage( "No AGLContext set" );
        return false;
    }

    // PBuffer
    const PixelViewport pvp = _window->getPixelViewport();
          AGLPbuffer    pbuffer;
    if( !aglCreatePBuffer( pvp.w, pvp.h, GL_TEXTURE_RECTANGLE_EXT, GL_RGBA,
                           0, &pbuffer ))
    {
        _window->setErrorMessage( "Could not create PBuffer: " + 
                                  ERROR( aglGetError( )));
        return false;
    }

    // attach to context
    if( !aglSetPBuffer( context, pbuffer, 0, 0, aglGetVirtualScreen( context )))
    {
        _window->setErrorMessage( "aglSetPBuffer failed: " +
                                  ERROR( aglGetError( )));
        return false;
    }

    setAGLPBuffer( pbuffer );
    return true;
}

bool AGLWindow::configInitAGLFullscreen()
{
    AGLContext context = getAGLContext();
    if( !context )
    {
        _window->setErrorMessage( "No AGLContext set" );
        return false;
    }

    Global::enterCarbon();

    aglEnable( context, AGL_FS_CAPTURE_SINGLE );

    const Pipe* pipe = getPipe();
    EQASSERT( pipe );

    const PixelViewport& pipePVP   = pipe->getPixelViewport();
    const PixelViewport& windowPVP = _window->getPixelViewport();
    const PixelViewport& pvp       = pipePVP.isValid() ? pipePVP : windowPVP;

#if 1
    if( !aglSetFullScreen( context, pvp.w, pvp.h, 0, 0 ))
        EQWARN << "aglSetFullScreen to " << pvp << " failed: " << aglGetError()
               << std::endl;
#else
    if( !aglSetFullScreen( context, 0, 0, 0, 0 ))
        EQWARN << "aglSetFullScreen failed: " << aglGetError() << std::endl;
#endif

    Global::leaveCarbon();
    _window->setPixelViewport( pvp );
    return true;
}

bool AGLWindow::configInitAGLWindow()
{
    AGLContext context = getAGLContext();
    if( !context )
    {
        _window->setErrorMessage( "No AGLContext set" );
        return false;
    }

    // window
    const bool decoration = getIAttribute(Window::IATTR_HINT_DECORATION) != OFF;
    WindowAttributes winAttributes = ( decoration ? 
                                       kWindowStandardDocumentAttributes :
                                       kWindowNoTitleBarAttribute | 
                                       kWindowNoShadowAttribute   |
                                       kWindowResizableAttribute  ) | 
        kWindowStandardHandlerAttribute | kWindowInWindowMenuAttribute;

    // top, left, bottom, right
    const PixelViewport   pvp = _window->getPixelViewport();
    const int32_t  menuHeight = decoration ? EQ_AGL_MENUBARHEIGHT : 0 ;
    Rect           windowRect = { pvp.y + menuHeight, pvp.x, 
                                  pvp.y + pvp.h + menuHeight,
                                  pvp.x + pvp.w };
    WindowRef      windowRef;

    Global::enterCarbon();
    const OSStatus status = CreateNewWindow( kDocumentWindowClass,
                                             winAttributes,
                                             &windowRect, &windowRef );
    if( status != noErr )
    {
        std::stringstream error;
        error << "Could not create carbon window: " << status;
        _window->setErrorMessage( error.str( ));
        Global::leaveCarbon();
        return false;
    }

    // window title
    const std::string& name = _window->getName();
    std::stringstream windowTitle;

    if( name.empty( ))
    {
        windowTitle << "Equalizer";
#ifndef NDEBUG
        windowTitle << " (" << getpid() << ")";
#endif;
    }
    else
        windowTitle << name;

    CFStringRef title = CFStringCreateWithCString( kCFAllocatorDefault,
                                                   windowTitle.str().c_str(),
                                                   kCFStringEncodingMacRoman );
    SetWindowTitleWithCFString( windowRef, title );
    CFRelease( title );
        
#ifdef LEOPARD
    if( !aglSetWindowRef( context, windowRef ))
    {
        _window->setErrorMessage( "aglSetWindowRef failed: " +
                                  ERROR( aglGetError( )));
        Global::leaveCarbon();
        return false;
    }
#else
    if( !aglSetDrawable( context, GetWindowPort( windowRef )))
    {
        _window->setErrorMessage( "aglSetDrawable failed: " +
                                  ERROR( aglGetError( )));
        Global::leaveCarbon();
        return false;
    }
#endif

    // show
    ShowWindow( windowRef );
    Global::leaveCarbon();
    setCarbonWindow( windowRef );

    return true;
}

void AGLWindow::setCarbonWindow( WindowRef window )
{
    EQINFO << "set Carbon window " << window << std::endl;

    if( _carbonWindow == window )
        return;

    if( _carbonWindow )
        exitEventHandler();
    _carbonWindow = window;

    if( !window )
        return;

    initEventHandler();

    Rect rect;
    Global::enterCarbon();
    if( GetWindowBounds( window, kWindowContentRgn, &rect ) == noErr )
    {
        PixelViewport pvp( rect.left, rect.top,
                           rect.right - rect.left, rect.bottom - rect.top );

        if( getIAttribute( Window::IATTR_HINT_DECORATION ) != OFF )
            pvp.y -= EQ_AGL_MENUBARHEIGHT;

        _window->setPixelViewport( pvp );
    }
    Global::leaveCarbon();
}

void AGLWindow::setAGLPBuffer( AGLPbuffer pbuffer )
{
    EQINFO << "set AGL PBuffer " << pbuffer << std::endl;

    if( _aglPBuffer == pbuffer )
        return;

    _aglPBuffer = pbuffer;

    if( !pbuffer )
        return;

    GLint         w;
    GLint         h;
    GLenum        target;
    GLenum        format;
    GLint         maxLevel;

    if( aglDescribePBuffer( pbuffer, &w, &h, &target, &format, &maxLevel ))
    {
        EQASSERT( target == GL_TEXTURE_RECTANGLE_EXT );

        const PixelViewport pvp( 0, 0, w, h );
        _window->setPixelViewport( pvp );
    }
}

void AGLWindow::initEventHandler()
{
    EQASSERT( !_eventHandler );
    _eventHandler = new AGLEventHandler( this );
}

void AGLWindow::exitEventHandler()
{
    delete _eventHandler;
    _eventHandler = 0;
}

}
