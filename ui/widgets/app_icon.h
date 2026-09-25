#pragma once

#include <QColor>
#include <QIcon>

class QPainter;

namespace app::ui {

enum class Icon : uint8_t {
    Cart,        // نقاط البيع
    Box,         // المنتجات
    People,      // العملاء
    Truck,       // الموردون
    Wallet,      // جلسة الصندوق
    Receipt,     // مبيعات اليوم
    Tag,         // المصاريف والسحوبات
    BarChart,    // التقارير
    Return,      // الاستردادات
    History,     // سجل المراجعة
    Gear,        // الإعدادات
    Search,      // البحث
    Plus,        // إضافة
    Check,       // نجاح
    Trash,       // حذف
    X,           // إلغاء
    Clock,       // وقت
    Info,        // معلومة
    Sun,         // فاتح
    Moon,        // داكن
    Shop,        // الشعار
};

// Returns a crisp pixmap of the given line icon. Results are cached.
QIcon appIcon(Icon kind, const QColor& color, int size = 24);

void paintIcon(Icon kind, QPainter& painter, const QColor& color, float s = 24.0f);

} // namespace app::ui