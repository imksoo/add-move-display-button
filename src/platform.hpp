#pragma once
#if defined(MTMB_OFFLINE_ABI) || defined(MTMB_FREESTANDING)
#error The unsupported no-SDK production build has been removed.
#endif
#if defined(MTMB_TEST_API)
#if defined(_WIN32)
#error Win32 doubles are for host-side simulations, not Windows binaries.
#endif
#include "../tests/win32_test_api.hpp"
#else
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <stdint.h>
#include <stddef.h>
#endif
