
/* Copyright (c) 2007-2008, Stefan Eilemann <eile@equalizergraphics.com> 
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

#include "pipeConnection.h"

#include "connectionDescription.h"
#include "node.h"

#include <eq/base/log.h>
#include <eq/base/thread.h>

using namespace eq::base;
using namespace std;

#ifdef WIN32

namespace eq
{
namespace net
{

PipeConnection::PipeConnection()
        : _readHandle( 0 ),
          _writeHandle( 0 ),
          _size( 0 ),
          _dataPending( CreateEvent( 0, TRUE, FALSE, 0 ))

{
    _description = new ConnectionDescription;
    _description->type = CONNECTIONTYPE_PIPE;
}

PipeConnection::~PipeConnection()
{
    close();
    CloseHandle( _dataPending );
}

//----------------------------------------------------------------------
// connect
//----------------------------------------------------------------------
bool PipeConnection::connect()
{
    EQASSERT( _description->type == CONNECTIONTYPE_PIPE );

    if( _state != STATE_CLOSED )
        return false;

    _state = STATE_CONNECTING;
    _size  = 0;

    if( !_createPipe( ))
    {
        close();
        return false;
    }

    _state = STATE_CONNECTED;
    _fireStateChanged();
    return true;
}

bool PipeConnection::_createPipe()
{
    if( CreatePipe( &_readHandle, &_writeHandle, 0, 0 ) == 0 )
    {
        EQERROR << "Could not create pipe: " << getErrorString( GetLastError( ))
                << endl;
        close();
        return false;
    }
    return true;
}

void PipeConnection::close()
{
    if( _readHandle )
    {
        CloseHandle( _readHandle );
        _readHandle = 0;
    }
    if( _writeHandle )
    {
        CloseHandle( _writeHandle );
        _writeHandle = 0;
    }
    _state = STATE_CLOSED;
    _fireStateChanged();
}
void PipeConnection::readNB( void* buffer, const uint64_t bytes ) { /* NOP */ }
int64_t PipeConnection::readSync( void* buffer, const uint64_t bytes )
{
    if( !_readHandle )
        return -1;

    DWORD bytesRead = 0;
    const BOOL ret = ReadFile( _readHandle, buffer, static_cast<DWORD>( bytes ),
                               &bytesRead, 0 );

    if( ret == 0 ) // Error
    {
        EQWARN << "Error during read: " << getErrorString( GetLastError( ))
               << endl;
        return -1;
    }

    if( bytesRead == 0 ) // EOF
    {
        close();
        return -1;
    }

    _mutex.set();
    EQASSERT( _size >= bytesRead );
    _size -= bytesRead;
    if( _size == 0 )
        ResetEvent( _dataPending );
    _mutex.unset();

    return bytesRead;
}

int64_t PipeConnection::write( const void* buffer, const uint64_t bytes )
{
    if( !_writeHandle )
        return -1;

    const DWORD write = EQ_MIN( static_cast<DWORD>( bytes ), 4096 );

    _mutex.set();
    _size += write; // speculatively 'write' everything
    _mutex.unset();

    DWORD bytesWritten = 0;
    const BOOL ret = WriteFile( _writeHandle, buffer, write, &bytesWritten, 0 );

    if( ret == 0 ) // Error
    {
        EQWARN << "Error during write: " << getErrorString( GetLastError( ))
               << endl;
        bytesWritten = 0;
    }

    _mutex.set();
    EQASSERT( _size >= write - bytesWritten );
    _size -= (write - bytesWritten); // correct size

    if( _size > 0 )
        SetEvent( _dataPending );
    else
    {
        EQASSERT( _size == 0 );
        ResetEvent( _dataPending );
    }
    _mutex.unset();

	if( ret==0 ) 
		return -1;
	return bytesWritten;
}
}
}
#else
#  error "File is only for WIN32 builds"
#endif