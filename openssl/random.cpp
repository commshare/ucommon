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

void Random::seed(void)
{
    secure::init();
    RAND_poll();
}

bool Random::seed(const unsigned char *buf, size_t size)
{
    secure::init();

    RAND_seed(buf, size);
    return true;
}

size_t Random::key(unsigned char *buf, size_t size)
{
    secure::init();

    if(RAND_bytes(buf, size))
        return size;
    return 0;
}

size_t Random::fill(unsigned char *buf, size_t size)
{
    secure::init();

    if(RAND_pseudo_bytes(buf, size))
        return size;
    return 0;
}

int Random::get(void)
{
    uint16_t v;;
    fill((unsigned char *)&v, sizeof(v));
    v /= 2;
    return (int)v;
}

int Random::get(int min, int max)
{
    unsigned rand;
    int range = max - min + 1;
    unsigned umax;

    if(max < min)
        return 0;

    memset(&umax, 0xff, sizeof(umax));

    do {
        fill((unsigned char *)&rand, sizeof(rand));
    } while(rand > umax - (umax % range));

    return min + (rand % range);
}

double Random::real(void)
{
    unsigned umax;
    unsigned rand;

    memset(&umax, 0xff, sizeof(umax));
    fill((unsigned char *)&rand, sizeof(rand));

    return ((double)rand) / ((double)umax);
}

double Random::real(double min, double max)
{
    return real() * (max - min) + min;
}

bool Random::status(void)
{
    if(RAND_status())
        return true;

    return false;
}

void Random::uuid(char *str)
{
    unsigned char buf[16];

    fill(buf, sizeof(buf));
    buf[6] &= 0x0f;
    buf[6] |= 0x40;
    buf[8] &= 0x3f;
    buf[8] |= 0x80;
    String::hexdump(buf, str, "4-2-2-2-6");
}

String Random::uuid(void)
{
    char buf[38];
    uuid(buf);
    return String(buf);
}

