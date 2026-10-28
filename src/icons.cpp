#include "icons.h"

#include <objidl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// GDI+ uses the min and max macros that NOMINMAX switches off.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace on {
namespace {

// Each icon is a few shapes in SVG terms, copied from public/icons.js:
// "C:cx cy r" is a circle, "R:x y w h rx" a rounded rectangle, and anything
// else is the d="" of a path.
std::vector<const char*> shapes(Icon icon) {
    switch (icon) {
        case Icon::Check: return {"M20 6 9 17l-5-5"};
        case Icon::Copy:
            return {"R:8 8 14 14 2",
                    "M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"};
        case Icon::Printer:
            return {"M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2",
                    "M6 9V3a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v6", "R:6 14 12 8 1"};
        case Icon::External:
            return {"M15 3h6v6", "M10 14 21 3",
                    "M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"};
        case Icon::Info: return {"C:12 12 10", "M12 16v-4", "M12 8h.01"};
        case Icon::Alert:
            return {"m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3",
                    "M12 9v4", "M12 17h.01"};
        case Icon::Send:
            return {"M14.536 21.686a.5.5 0 0 0 .937-.024l6.5-19a.496.496 0 0 0-.635-.635l-19 "
                    "6.5a.5.5 0 0 0-.024.937l7.93 3.18a2 2 0 0 1 1.112 1.11z",
                    "m21.854 2.147-10.94 10.939"};
        case Icon::Bell:
            return {"M10.268 21a2 2 0 0 0 3.464 0",
                    "M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 "
                    "12.499 18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326"};
        case Icon::Globe:
            return {"C:12 12 10", "M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20",
                    "M2 12h20"};
        case Icon::More: return {"C:12 12 1", "C:19 12 1", "C:5 12 1"};
    }
    return {};
}

// Reads the numbers and flags of an SVG path, which may be packed together
// ("a.5.5 0 0 0 .937-.024").
struct Reader {
    const char* p;

    void skip() {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    }
    bool atNumber() {
        skip();
        return std::isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.';
    }
    float number() {
        skip();
        const char* s = p;
        if (*p == '+' || *p == '-') ++p;
        while (std::isdigit((unsigned char)*p)) ++p;
        if (*p == '.') {
            ++p;
            while (std::isdigit((unsigned char)*p)) ++p;
        }
        if (*p == 'e' || *p == 'E') {
            ++p;
            if (*p == '+' || *p == '-') ++p;
            while (std::isdigit((unsigned char)*p)) ++p;
        }
        return std::strtof(std::string(s, p).c_str(), nullptr);
    }
    int flag() {
        skip();
        const int f = *p == '1' ? 1 : 0;
        if (*p == '0' || *p == '1') ++p;
        return f;
    }
};

// From the icon's 24-unit grid to pixels.
struct Xf {
    float x, y, s;
    Gdiplus::PointF operator()(float px, float py) const {
        return Gdiplus::PointF(x + px * s, y + py * s);
    }
};

// An SVG elliptical arc, as cubic Béziers (SVG 1.1 implementation notes,
// F.6.5 and F.6.6).
void addArc(Gdiplus::GraphicsPath& gp, const Xf& xf, float x1, float y1, float rx, float ry,
            float phiDeg, int large, int sweep, float x2, float y2) {
    if (x1 == x2 && y1 == y2) return;
    if (rx == 0 || ry == 0) {
        gp.AddLine(xf(x1, y1), xf(x2, y2));
        return;
    }
    const double pi = 3.14159265358979323846;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double phi = phiDeg * pi / 180, cs = std::cos(phi), sn = std::sin(phi);
    const double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
    const double x1p = cs * dx + sn * dy, y1p = -sn * dx + cs * dy;
    double rxd = rx, ryd = ry;
    const double lambda = (x1p * x1p) / (rxd * rxd) + (y1p * y1p) / (ryd * ryd);
    if (lambda > 1) {
        rxd *= std::sqrt(lambda);
        ryd *= std::sqrt(lambda);
    }
    const double num = rxd * rxd * ryd * ryd - rxd * rxd * y1p * y1p - ryd * ryd * x1p * x1p;
    const double den = rxd * rxd * y1p * y1p + ryd * ryd * x1p * x1p;
    double coef = den == 0 ? 0 : std::sqrt(std::max(0.0, num / den));
    if (large == sweep) coef = -coef;
    const double cxp = coef * rxd * y1p / ryd, cyp = -coef * ryd * x1p / rxd;
    const double cx = cs * cxp - sn * cyp + (x1 + x2) / 2.0;
    const double cy = sn * cxp + cs * cyp + (y1 + y2) / 2.0;
    auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double t1 = angle(1, 0, (x1p - cxp) / rxd, (y1p - cyp) / ryd);
    double dt = angle((x1p - cxp) / rxd, (y1p - cyp) / ryd, (-x1p - cxp) / rxd,
                      (-y1p - cyp) / ryd);
    if (!sweep && dt > 0) dt -= 2 * pi;
    if (sweep && dt < 0) dt += 2 * pi;
    const int segs = std::max(1, (int)std::ceil(std::fabs(dt) / (pi / 2) - 1e-9));
    const double delta = dt / segs, k = 4.0 / 3.0 * std::tan(delta / 4);
    auto map = [&](double ux, double uy) {
        return xf((float)(cx + rxd * ux * cs - ryd * uy * sn),
                  (float)(cy + rxd * ux * sn + ryd * uy * cs));
    };
    for (int i = 0; i < segs; ++i) {
        const double a1 = t1 + i * delta, a2 = a1 + delta;
        const double c1x = std::cos(a1) - k * std::sin(a1), c1y = std::sin(a1) + k * std::cos(a1);
        const double c2x = std::cos(a2) + k * std::sin(a2), c2y = std::sin(a2) - k * std::cos(a2);
        gp.AddBezier(map(std::cos(a1), std::sin(a1)), map(c1x, c1y), map(c2x, c2y),
                     map(std::cos(a2), std::sin(a2)));
    }
}

void addPath(Gdiplus::GraphicsPath& gp, const char* d, const Xf& xf) {
    Reader r{d};
    char cmd = 0;
    float cx = 0, cy = 0, sx = 0, sy = 0, lx = 0, ly = 0;
    bool lastCubic = false;
    for (;;) {
        r.skip();
        if (!*r.p) break;
        if (std::isalpha((unsigned char)*r.p)) cmd = *r.p++;
        else if (!cmd || !r.atNumber()) break;
        const bool rel = std::islower((unsigned char)cmd) != 0;
        const char c = (char)std::toupper((unsigned char)cmd);
        const float ox = rel ? cx : 0, oy = rel ? cy : 0;
        bool cubic = false;
        switch (c) {
            case 'M': {
                const float x = r.number() + ox, y = r.number() + oy;
                gp.StartFigure();
                cx = sx = x;
                cy = sy = y;
                cmd = rel ? 'l' : 'L';   // more pairs after a move are lines
                break;
            }
            case 'L': {
                const float x = r.number() + ox, y = r.number() + oy;
                gp.AddLine(xf(cx, cy), xf(x, y));
                cx = x;
                cy = y;
                break;
            }
            case 'H': {
                const float x = r.number() + ox;
                gp.AddLine(xf(cx, cy), xf(x, cy));
                cx = x;
                break;
            }
            case 'V': {
                const float y = r.number() + oy;
                gp.AddLine(xf(cx, cy), xf(cx, y));
                cy = y;
                break;
            }
            case 'C': {
                const float x1 = r.number() + ox, y1 = r.number() + oy;
                const float x2 = r.number() + ox, y2 = r.number() + oy;
                const float x = r.number() + ox, y = r.number() + oy;
                gp.AddBezier(xf(cx, cy), xf(x1, y1), xf(x2, y2), xf(x, y));
                lx = x2;
                ly = y2;
                cx = x;
                cy = y;
                cubic = true;
                break;
            }
            case 'S': {
                const float x1 = lastCubic ? 2 * cx - lx : cx, y1 = lastCubic ? 2 * cy - ly : cy;
                const float x2 = r.number() + ox, y2 = r.number() + oy;
                const float x = r.number() + ox, y = r.number() + oy;
                gp.AddBezier(xf(cx, cy), xf(x1, y1), xf(x2, y2), xf(x, y));
                lx = x2;
                ly = y2;
                cx = x;
                cy = y;
                cubic = true;
                break;
            }
            case 'Q': {
                const float qx = r.number() + ox, qy = r.number() + oy;
                const float x = r.number() + ox, y = r.number() + oy;
                gp.AddBezier(xf(cx, cy), xf(cx + 2.f / 3 * (qx - cx), cy + 2.f / 3 * (qy - cy)),
                             xf(x + 2.f / 3 * (qx - x), y + 2.f / 3 * (qy - y)), xf(x, y));
                cx = x;
                cy = y;
                break;
            }
            case 'A': {
                const float rx = r.number(), ry = r.number(), rot = r.number();
                const int large = r.flag(), sweep = r.flag();
                const float x = r.number() + ox, y = r.number() + oy;
                addArc(gp, xf, cx, cy, rx, ry, rot, large, sweep, x, y);
                cx = x;
                cy = y;
                break;
            }
            case 'Z':
                gp.CloseFigure();
                cx = sx;
                cy = sy;
                cmd = 0;   // nothing may follow a close without a new command
                break;
            default:
                return;    // a command these icons never use
        }
        lastCubic = cubic;
    }
}

void roundRect(Gdiplus::GraphicsPath& gp, const Xf& xf, float x, float y, float w, float h,
               float rad) {
    const Gdiplus::PointF tl = xf(x, y);
    const float W = w * xf.s, H = h * xf.s, d = 2 * rad * xf.s;
    gp.StartFigure();
    if (d <= 0) {
        gp.AddRectangle(Gdiplus::RectF(tl.X, tl.Y, W, H));
        return;
    }
    gp.AddArc(tl.X, tl.Y, d, d, 180, 90);
    gp.AddArc(tl.X + W - d, tl.Y, d, d, 270, 90);
    gp.AddArc(tl.X + W - d, tl.Y + H - d, d, d, 0, 90);
    gp.AddArc(tl.X, tl.Y + H - d, d, d, 90, 90);
    gp.CloseFigure();
}

}  // namespace

void drawIcon(HDC dc, Icon icon, int x, int y, int size, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Xf xf{(float)x, (float)y, size / 24.f};
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)),
                     2 * xf.s);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    for (const char* part : shapes(icon)) {
        Gdiplus::GraphicsPath gp;
        if (part[0] == 'C' && part[1] == ':') {
            Reader r{part + 2};
            const float cx = r.number(), cy = r.number(), rad = r.number();
            const Gdiplus::PointF tl = xf(cx - rad, cy - rad);
            gp.AddEllipse(tl.X, tl.Y, 2 * rad * xf.s, 2 * rad * xf.s);
        } else if (part[0] == 'R' && part[1] == ':') {
            Reader r{part + 2};
            const float rx = r.number(), ry = r.number(), w = r.number(), h = r.number(),
                        rad = r.number();
            roundRect(gp, xf, rx, ry, w, h, rad);
        } else {
            addPath(gp, part, xf);
        }
        g.DrawPath(&pen, &gp);
    }
}

}  // namespace on
