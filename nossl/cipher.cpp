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

#include "local.h"

static const unsigned char *_salt = NULL;
static unsigned _rounds = 1;

Cipher::Key::Key(const char *cipher)
{
    secure::init();
    set(cipher);
}

Cipher::Key::Key(const char *cipher, const char *digest)
{
    secure::init();
    set(cipher, digest);
}

Cipher::Key::Key(const char *cipher, const char *digest, const char *text, size_t size, const unsigned char *salt, unsigned rounds)
{
    secure::init();
    set(cipher, digest);
    assign(text, size, salt, rounds);
}

void Cipher::Key::assign(const char *text, size_t size, const unsigned char *salt, unsigned rounds)
{
    keysize = 0;
}

Cipher::Key::Key()
{
    secure::init();
    clear();
}

Cipher::Key::~Key()
{
    clear();
}

void Cipher::Key::set(const char *cipher)
{
    clear();
}

void Cipher::Key::set(const char *cipher, const char *digest)
{
    set(cipher);
}

void Cipher::Key::assign(const char *text, size_t size)
{
    assign(text, size, _salt, _rounds);
}

void Cipher::Key::clear(void)
{
    algotype = NULL;
    hashtype = NULL;
    keysize = 0;
    zerofill(keybuf, sizeof(keybuf));
    zerofill(ivbuf, sizeof(ivbuf));
}

void Cipher::Key::options(const unsigned char *salt, unsigned rounds)
{
    _salt = salt;
    _rounds = rounds;
}

Cipher::Cipher(key_t key, mode_t mode, unsigned char *address, size_t size)
{
    bufaddr = NULL;
    bufsize = bufpos = 0;
    context = NULL;
}

Cipher::Cipher()
{
    bufaddr = NULL;
    bufsize = bufpos = 0;
    context = NULL;
}

Cipher::~Cipher()
{
    flush();
    release();
}

bool Cipher::has(const char *id)
{
    return false;
}

void Cipher::push(unsigned char *address, size_t size)
{
}

void Cipher::release(void)
{
}

size_t Cipher::flush(void)
{
    return 0;
}

size_t Cipher::puts(const char *text)
{
    if(!text)
        return 0;

    return put((const unsigned char *)text, strlen(text));
}

void Cipher::set(unsigned char *address, size_t size)
{
    flush();
    bufaddr = address;
    bufsize = size;
    bufpos = 0;
}

void Cipher::set(key_t key, mode_t mode, unsigned char *address, size_t size)
{
    release();

    bufsize = size;
    bufmode = mode;
    bufaddr = address;

    memcpy(&keys, key, sizeof(keys));
}

size_t Cipher::put(const unsigned char *data, size_t size)
{
    size_t count = 0;

    while(bufsize && size + bufpos > bufsize) {
        size_t diff = bufsize - bufpos;
        count += put(data, diff);
        data += diff;
        size -= diff;
    }

    return 0;
}

size_t Cipher::pad(const unsigned char *data, size_t size)
{
    return 0;
}

size_t Cipher::process(unsigned char *data, size_t size, bool flag)
{
    return 0;
}
