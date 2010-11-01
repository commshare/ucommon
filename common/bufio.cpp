// Copyright (C) 2010 David Sugar, Tycho Softworks.
//
// This file is part of GNU uCommon C++.
//
// GNU uCommon C++ is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GNU uCommon C++ is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with GNU uCommon C++.  If not, see <http://www.gnu.org/licenses/>.

#include <config.h>
#include <ucommon/buffer.h>
#include <ucommon/string.h>

using namespace UCOMMON_NAMESPACE;

fbuf::fbuf() : BufferProtocol(), fsys()
{
}

fbuf::fbuf(const char *path, access_t access, size_t size)
{
    open(path, access, size);
}

fbuf::fbuf(const char *path, access_t access, unsigned mode, size_t size)
{
    create(path, access, mode, size);
}

fbuf::~fbuf()
{
    fbuf::close();
}

int fbuf::_err(void) const
{
    return error;
}

void fbuf::_clear(void)
{
    error = 0;
}

void fbuf::open(const char *path, access_t mode, size_t size)
{
    fbuf::close();
    if(mode != ACCESS_DIRECTORY)
        fsys::open(path, mode);
    if(getfile() == INVALID_HANDLE_VALUE)
        return;

    inpos = outpos = 0;

    switch(mode) {
    case ACCESS_RDONLY:
        allocate(size, BUF_RD);
        break;
    case ACCESS_STREAM:
    case ACCESS_WRONLY:
        allocate(size, BUF_WR);
        break;
    case ACCESS_RANDOM:
    case ACCESS_SHARED:
    case ACCESS_REWRITE:
        allocate(size, BUF_RDWR);
        break;
    case ACCESS_APPEND:
        outpos = fsys::end;
        allocate(size, BUF_WR);
    default:
        break;
    }
}

void fbuf::create(const char *path, access_t mode, unsigned cmode, size_t size)
{
    fbuf::close();
    if(mode != ACCESS_DIRECTORY)
        fsys::create(path, mode, cmode);
    if(getfile() == INVALID_HANDLE_VALUE)
        return;

    inpos = outpos = 0;

    switch(mode) {
    case ACCESS_RDONLY:
        allocate(size, BUF_RD);
        break;
    case ACCESS_STREAM:
    case ACCESS_WRONLY:
        allocate(size, BUF_WR);
        break;
    case ACCESS_RANDOM:
    case ACCESS_SHARED:
    case ACCESS_REWRITE:
        allocate(size, BUF_RDWR);
        break;
    case ACCESS_APPEND:
        outpos = fsys::end;
        allocate(size, BUF_WR);
    default:
        break;
    }
}

void fbuf::close()
{
    BufferProtocol::release();
    fsys::close();
}

fsys::offset_t fbuf::tell(void)
{
    if(!fbuf::is_open())
        return 0;

    if(is_input())
        return inpos + unread();

    if(outpos == fsys::end)
        return fsys::end;

    return outpos + unsaved();
}

bool fbuf::trunc(offset_t offset)
{
    int seekerr;

    if(!fbuf::is_open())
        return false;

    _clear();
    reset();
    flush();

    seekerr = trunc(offset);
    if(!seekerr)
        inpos = outpos = offset;

    if(fsys::err())
        return false;

    return true;
}

bool fbuf::seek(offset_t offset)
{
    int seekerr;

    if(!fbuf::is_open())
        return false;

    _clear();
    reset();
    flush();

    seekerr = seek(offset);
    if(!seekerr)
        inpos = outpos = offset;

    if(fsys::err())
        return false;

    return true;
}

size_t fbuf::_push(const char *buf, size_t size)
{
    ssize_t result;

    if(outpos == fsys::end) {
        result = fsys::write(buf, size);
        if(result < 0)
            result = 0;

        return (size_t) result;
    }

#ifdef  HAVE_PWRITE
    result = pwrite(getfile(), buf, size, outpos);
    if(result < 0)
        result = 0;
    outpos += result;
    return (size_t)result;
#else
    int seekerr;

    if(is_input()) {
        // if read & write separate threads, protect i/o reposition
        mutex::protect(this);
        seekerr = fsys::seek(outpos);
        if(seekerr) {
            mutex::release(this);
            return 0;
        }
    }

    result = fsys::write(buf, size);

    if(is_input()) {
        seekerr = fsys::seek(inpos);
        mutex::release(this);
        if(result >= 0 && seekerr)
            seteof();
    }

    if(result < 0)
        result = 0;

    outpos += result;
    return (size_t)result;
#endif
}

size_t fbuf::_pull(char *buf, size_t size)
{
    ssize_t result;

#ifdef  HAVE_PWRITE
    if(is_output())
        result = pread(getfile(), buf, size, inpos);
    else
        result = fsys::read(buf, size);
#else

    if(is_output())
        mutex::protect(this);

    result = fsys::read(buf, size);

    if(is_output())
        mutex::release(this);
#endif

    if(result < 0)
        result = 0;
    inpos += result;
    return (size_t)result;
}

TCPBuffer::TCPBuffer() : BufferProtocol()
{
    so = INVALID_SOCKET;
}

TCPBuffer::TCPBuffer(const char *host, const char *service, size_t size) :
BufferProtocol()
{
    so = INVALID_SOCKET;
    open(host, service, size);
}

TCPBuffer::TCPBuffer(const TCPServer *server, size_t size) :
BufferProtocol()
{
    so = INVALID_SOCKET;
    open(server, size);
}

TCPBuffer::~TCPBuffer()
{
    TCPBuffer::close();
}

void TCPBuffer::open(const char *host, const char *service, size_t size)
{
    struct addrinfo *list = Socket::getaddress(host, service, SOCK_STREAM, 0);
    if(!list)
        return;

    so = Socket::create(list, SOCK_STREAM, 0);
    Socket::release(list);
    if(so == INVALID_SOCKET)
        return;

    _buffer(size);
}

void TCPBuffer::open(const TCPServer *server, size_t size)
{
    close();
    so = server->accept();
    if(so == INVALID_SOCKET)
        return;

    _buffer(size);
}

void TCPBuffer::close(void)
{
    if(so == INVALID_SOCKET)
        return;

    BufferProtocol::release();
    Socket::release(so);
    so = INVALID_SOCKET;
}

void TCPBuffer::_buffer(size_t size)
{
    unsigned iobuf = 0;
    unsigned mss = size;
    unsigned max = 0;

#ifdef  TCP_MAXSEG
    socklen_t alen = sizeof(max);
#endif

    if(size < 80) {
        allocate(size, BUF_RDWR);
        return;
    }

#ifdef  TCP_MAXSEG
    if(mss)
        setsockopt(so, IPPROTO_TCP, TCP_MAXSEG, (char *)&max, sizeof(max));
    getsockopt(so, IPPROTO_TCP, TCP_MAXSEG, (char *)&max, &alen);
#endif

    if(max && max < mss)
        mss = max;

    if(!mss) {
        if(max)
            mss = max;
        else
            mss = 536;
        goto alloc;
    }

    if(mss < 80)
        mss = 80;

    if(mss * 7 < 64000u)
        iobuf = mss * 7;
    else if(size * 6 < 64000u)
        iobuf = mss * 6;
    else
        iobuf = mss * 5;

    Socket::sendsize(so, iobuf);
    Socket::recvsize(so, iobuf);

    if(mss < 512)
        Socket::sendwait(so, mss * 4);

alloc:
    allocate(size, BUF_RDWR);
}

int TCPBuffer::_err(void) const
{
    return ioerr;
}

void TCPBuffer::_clear(void)
{
    ioerr = 0;
}

bool TCPBuffer::_blocking(void)
{
    if(iowait)
        return true;

    return false;
}

size_t TCPBuffer::_push(const char *address, size_t len)
{
    if(ioerr)
        return 0;

    ssize_t result = writeto(address, len);
    if(result < 0)
        result = 0;

    return (size_t)result;
}

size_t TCPBuffer::_pull(char *address, size_t len)
{
    ssize_t result;

    result = readfrom(address, len);

    if(result < 0)
        result = 0;
    return (size_t)result;
}

bool TCPBuffer::_pending(void)
{
    if(unread())
        return true;

    if(is_input() && iowait && iowait != Timer::inf)
        return Socket::wait(so, iowait);

    return Socket::wait(so, 0);
}
