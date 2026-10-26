# Third-party components

The binary uses portions of Steinberg VST3 SDK 3.8.1 under the MIT license.
See Steinberg-VST3-LICENSE.txt. Source: https://github.com/steinbergmedia/vst3sdk

LLVM-MinGW supplies statically linked libc++ / LLVM runtime code under the
Apache 2.0 license with LLVM exceptions and applicable legacy LLVM notices.
See LLVM-MinGW-LICENSE.txt. Toolchain source: https://github.com/mstorsjo/llvm-mingw

The toolchain also supplies the MinGW-w64 CRT and Windows API import libraries.
MinGW-w64 source and component license notices: https://github.com/mingw-w64/mingw-w64

pluginval is used as an external test tool only; it is not included in or linked
into this plugin. https://github.com/Tracktion/pluginval
