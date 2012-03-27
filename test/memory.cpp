// Copyright (C) 2006-2010 David Sugar, Tycho Softworks.
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

#ifndef DEBUG
#define DEBUG
#endif

#include <ucommon/ucommon.h>

#include <stdio.h>

using namespace UCOMMON_NAMESPACE;

extern "C" int main()
{
    stringlist_t mylist;
    stringitem_t *item;

    mylist.add("100");
    mylist.add("050");
    mylist.add("300");

    item = mylist.begin();
    assert(eq("100", mylist[0]));
    mylist.sort();
    item = mylist.begin();
    assert(eq(item->get(), "050"));

    assert(eq(mylist[1], "100"));
    assert(eq(mylist[2], "300"));

    return 0;
}