/*
  Copyright 2009 Larry Gritz and the other authors and contributors.
  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the software's owners nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  (This is the Modified BSD License)
*/

// Python.h uses the 'register' keyword, don't warn about it being
// deprecated in C++17.
#if (__cplusplus >= 201703L && defined(__GNUC__))
#    pragma GCC diagnostic ignored "-Wregister"
#endif

// Must include Python.h first to avoid certain warnings
#include <Python.h>

#include <pybind11/pybind11.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include <OSL/oslversion.h>
#include <OSL/oslquery.h>

using namespace OSL;
namespace py = pybind11;

namespace PyOSL {


// This OSL_DECLARE_PYMODULE mojo is necessary if we want to pass in the
// MODULE name as a #define. Google for Argument-Prescan for additional
// info on why this is necessary

#define OSL_DECLARE_PYMODULE(x) PYBIND11_MODULE(x, m)

OSL_DECLARE_PYMODULE(OSL_PYMODULE_NAME)
{
    m.attr("osl_version")         = OSL_VERSION;
    m.attr("VERSION")             = OSL_VERSION;
    m.attr("VERSION_STRING")      = OSL_LIBRARY_VERSION_STRING;
    m.attr("VERSION_MAJOR")       = OSL_VERSION_MAJOR;
    m.attr("VERSION_MINOR")       = OSL_VERSION_MINOR;
    m.attr("VERSION_PATCH")       = OSL_VERSION_PATCH;
}

}  // namespace PyOSL
