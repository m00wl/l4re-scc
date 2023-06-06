// std::messages implementation details, IEEE 1003.1-200x version -*- C++ -*-

// Copyright (C) 2001-2020 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

//
// ISO C++ 14882: 22.2.7.1.2  messages virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std _GLIBCXX_VISIBILITY(default)
{
  // Specializations
  template<>
    string
    messages<char>::do_get(catalog __c, int __setid, int __msgid,
			   const string& __dfault) const
    {
      nl_catd __nlc = reinterpret_cast<nl_catd>(__c);
      return string(catgets(__nlc, __setid, __msgid, __dfault.c_str()));
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    wstring
    messages<wchar_t>::do_get(catalog, int, int, const wstring& __dfault) const
    {
      nl_catd __nlc = reinterpret_cast<nl_catd>(__c);
      char* __msg = catgets(__nlc, __setid, __msgid,
			    _M_convert_to_char(__dfault));
      return _M_convert_from_char(__msg);
    }
#endif
}
