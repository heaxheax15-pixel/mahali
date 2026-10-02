#pragma once

#include <QtGlobal>

namespace app::ui::themeTokens {

inline constexpr int space4 = 4;
inline constexpr int space8 = 8;
inline constexpr int space12 = 12;
inline constexpr int space16 = 16;

inline constexpr int pageInset = space12;
inline constexpr int sidebarWidth = 196;
inline constexpr int topBarHeight = 56;

inline constexpr int fontBodyPt = 12;
inline constexpr int fontHeadingPt = 18;
inline constexpr int fontNotePt = 11;

struct ThemeColors {
    quint32 window;
    quint32 surface;
    quint32 border;
    quint32 text;
    quint32 textSecondary;
    quint32 muted;
    quint32 accent;
    quint32 accentSoft;
    quint32 positive;
    quint32 negative;
    quint32 warning;
};

inline constexpr ThemeColors lightColors = {
    0xffeef2f7, 0xffffffff, 0xffcbd5e1, 0xff0f172a, 0xff475569, 0xff64748b,
    0xff2563eb, 0xffdbeafe, 0xff16a34a, 0xffdc2626, 0xffd97706,
};

inline constexpr ThemeColors darkColors = {
    0xff0a0a0a, 0xff1a1a1a, 0xff2d2d2d, 0xfffafafa, 0xffa3a3a3, 0xff737373,
    0xffd4a017, 0xff2d1f04, 0xff22c55e, 0xffef4444, 0xffb8860b,
};

} // namespace app::ui::themeTokens
