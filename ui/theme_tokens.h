#pragma once

#include <QtGlobal>

namespace app::ui::themeTokens {

inline constexpr int space4 = 4;
inline constexpr int space8 = 8;
inline constexpr int space12 = 12;
inline constexpr int space16 = 16;
inline constexpr int space24 = 24;

inline constexpr int pageInset = space12;
inline constexpr int sidebarWidth = 96;
inline constexpr int topBarHeight = 44;
inline constexpr int buttonMinHeight = 40;
inline constexpr int primaryActionHeight = 56;
inline constexpr int tableRowHeight = 40;
inline constexpr int tableHeaderHeight = 34;

inline constexpr int fontBodyPt = 12;
inline constexpr int fontHeadingPt = 18;
inline constexpr int fontTablePt = 13;
inline constexpr int fontProductPt = 14;
inline constexpr int fontCardValuePt = 24;
inline constexpr int fontTotalPt = 30;
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
    quint32 positiveSoft;
    quint32 negativeSoft;
};

inline constexpr ThemeColors lightColors = {
    0xffeef2f7, 0xffffffff, 0xffcbd5e1, 0xff0f172a, 0xff475569, 0xff64748b,
    0xff1d4ed8, 0xffdbeafe, 0xff166534, 0xffb91c1c, 0xffb45309,
    0xffdcfce7, 0xfffee2e2,
};

inline constexpr ThemeColors darkColors = {
    0xff0a0a0a, 0xff1a1a1a, 0xff2d2d2d, 0xfffafafa, 0xffa3a3a3, 0xff737373,
    0xffd4a017, 0xff2d1f04, 0xff86efac, 0xfffca5a5, 0xfffbbf24,
    0xff14532d, 0xff7f1d1d,
};

} // namespace app::ui::themeTokens
