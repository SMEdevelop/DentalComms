// The SMEdevelop design system's values for the server window: the same
// colours, type sizes, spacing and rounding the web pages read from
// public/sme/tokens.css. Change the design system first, then both copies.
#pragma once
#include <windows.h>

namespace on {
namespace theme {

constexpr COLORREF hex(unsigned v) {
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ---- colour
constexpr COLORREF surface = hex(0xf7f3ec);         // page: warm oat paper
constexpr COLORREF surfaceRaised = hex(0xfffdf9);   // cards, the action bar
constexpr COLORREF surfaceSunken = hex(0xefe8dc);   // quiet buttons, wells
constexpr COLORREF surfaceHover = hex(0xe6dccb);    // hover on sunken
constexpr COLORREF line = hex(0xe3d9c8);            // card outlines, dividers
constexpr COLORREF lineStrong = hex(0x9a8a74);      // edges of controls
constexpr COLORREF ink = hex(0x3a3027);             // all text
constexpr COLORREF inkMuted = hex(0x6e5f4e);        // secondary text
constexpr COLORREF sky100 = hex(0xe3eef6);          // light blue wash
constexpr COLORREF sky300 = hex(0xb9d3e6);          // decoration only
constexpr COLORREF sand100 = hex(0xf1e6d8);         // light brown wash
constexpr COLORREF clay = hex(0xa0704a);            // icons on sand
constexpr COLORREF primary = hex(0x3d6b8e);         // the one main action
constexpr COLORREF primaryHover = hex(0x335c7b);
constexpr COLORREF onPrimary = hex(0xffffff);
constexpr COLORREF primaryTint = sky100;
constexpr COLORREF primaryInk = hex(0x2f5878);      // blue text
constexpr COLORREF focusRing = hex(0x3d6b8e);
constexpr COLORREF success = hex(0x4f7a52);         // sage
constexpr COLORREF successTint = hex(0xe5efe2);
constexpr COLORREF warning = hex(0xc98a2b);         // honey: edges only
constexpr COLORREF warningTint = hex(0xfaf0d9);
constexpr COLORREF warningInk = hex(0x8a5a12);
constexpr COLORREF danger = hex(0xb4483a);
constexpr COLORREF dangerTint = hex(0xf8e5e0);
constexpr COLORREF dangerInk = hex(0xa03f32);

// ---- type (px at 96 DPI): size, line height, weight
struct TypeStyle {
    int size, line, weight;
};
constexpr TypeStyle title{26, 32, 700};
constexpr TypeStyle tileLabel{24, 30, 700};
constexpr TypeStyle button{18, 24, 600};
constexpr TypeStyle buttonBig{20, 26, 700};
constexpr TypeStyle body{17, 26, 400};
constexpr TypeStyle label{15, 20, 600};
constexpr TypeStyle overline{13, 18, 700};
constexpr TypeStyle banner{17, 24, 700};

// ---- spacing
constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space5 = 24,
              space6 = 32, space7 = 48;

// ---- shape
constexpr int radiusSm = 8, radiusMd = 16, radiusLg = 24;

// ---- tap sizes
constexpr int targetMin = 48, targetBig = 64;

}  // namespace theme
}  // namespace on
