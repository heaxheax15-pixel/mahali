# mahali — سياق المشروع

> المرجع الكامل: `CONTEXT_DUMP.md`. هذا ملخص تنفيذي.

## ما هو
نظام نقاط بيع + مخزون + محاسبة + مزامنة LAN، offline-first، لمتجر بقالة.
واجهة عربية RTL. C++17 / Qt 6 / SQLite. الإصدار 1.0.4.

## البنية
core → منطق، لا Qt GUI (Qt Core/Network فقط)
data → QtSql repositories + domain services + schema/migrations
network → QHttpServer للمزامنة + عميل + بروتوكول
ui → QtWidgets، صفحات/حوارات/ثيمات
apps/desktop/main.cpp → نقطة الدخول

قاعدة: core لا يعتمد على Qt GUI. i18n.cpp و zakat_notifier.cpp في core/ لكن يُترجمان في data/ (يقرآن SettingRepository).

## ثوابت لا تُخترق
- المال دائماً `long long` سنتات. لا floats. الفائض يُفحَص لا يُنفَّذ.
- كل كتابة مالية داخل service + transaction. الخدمة تكتب audit_log.
- الجداول المحاسبية append-only: العكس = صف سالب معكوس، لا UPDATE/DELETE.
- `total_cents == Σ(lines) + adjustment_cents` (ينطبق على العكس).
- `cogs` يُشتق من `unit_cost_cents` المجمّد وقت البيع.
- رصيد العميل = opening + Σ(transactions) − Σ(payments) — **بلا فلترة إشارة**.
- `pieces_consumed` دائماً مقدار موجب (count). الإشارة في `quantity`.
- جلسة صندوق مفتوحة واحدة كحد أقصى.
- عكس واحد لكل أصل (5 partial unique indexes).
- timestamps ISO-8601 UTC.

## حقائق تشغيلية
- `runSchemaMigrations()` يختم `user_version` بلا شرط. مقصود.
- كل ترحيل idempotent (فحص `PRAGMA table_info`).
- `resolveSaleItems()` هي **المصدر الوحيد** الذي يملأ `unitKind` و`piecesConsumed`.
- الحارس في `reverseSale`/`reverseCustomerDebt` يرفض `piecesConsumed <= 0`.
- `CashSessionRepository::open(0)` مرفوض — يحتاج float غير صفري.
- `parseMoney` يرفض السالب؛ pos_page يعالج `+`/`-` يدوياً.
- `lupdate`: شغّله على `ui data core apps` دائماً. حذف `apps` يحذف مدخلات حية.
- CI يبني `mahali-desktop` فقط. **الاختبارات لا تُشغَّل في CI.**

## قياسات حالية
- 16 اختبار CTest، 16/16 أخضر.
- ar + fr: 606/606 مكتمل. en: 384 رسالة، 292 unfinished (placeholder).
- الحوار: حقل "عدد الحبات في العلبة" + "اسم العلبة" + "الكمية الافتتاحية" (للمنتج الجديد).
- POS: عمود Unité قائمة منسدلة (قطعة / اسم العلبة).
- باركود العلبة = باركود الحبة + "c" (يُولَّد عند الحفظ).