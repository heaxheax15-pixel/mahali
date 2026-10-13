# TASKS — قائمة العمل

## 🔴 حرجة
- [ ] **Ajustement + المزامنة**: `SyncOperation` لا يحمل `adjustment_cents`، و`SyncProcessor::applySale` لا يمرّرها. بيع من جهاز مع adjustment يُزامن الإجمالي ويفقد العمود على السيرفر.
- [ ] **مفتاح HMAC افتراضي ثابت** في `apps/desktop/main.cpp:72`. المحلات التي لم تفتح الإعدادات تشترك في نفس المفتاح.
- [ ] **`apps/android/main.cpp` مفقود**. `-DMAHALI_ANDROID=ON` مكسور.

## 🟡 مهمة
- [ ] `refreshTotals()` في POS يسجّل "Unités: N" بـ`line.quantity`. الكرتونة تُحسب 1 لا 24. قرار UX مطلوب.
- [ ] `sync_hmac_key` الافتراضي يُغيَّر عند أول تشغيل.
- [ ] `13cba43` "WIP: audit fixes (unverified)" — يستحق مراجعة.
- [ ] `.vscode/launch.json` يشغّل Chrome — مخلّف.
- [ ] `device_ledger_service.cpp` يستدعي `m_sales.insert` المشترك، فيكتب `receipt_number = NULL`. يحتاج استدعاء `mintReceiptNumber` داخل transaction الخاص به. **يُنفَّذ عند أول زبون بحاسوبين فعليين.**

## 🟡 مقبولة (لن تُصلَح، موثَّقة)
- [x] `Q_OBJECT` تحذيرات `lupdate` في ثلاثة ملفات:
  - `purchase_dialog.cpp:104` و`select_customer_dialog.cpp:58` — **تحذيران كاذبان**: هما namespace + دالة حرّة، لا صنف.
  - `mini_bar_chart.cpp:19` — صنف حقيقي بلا `Q_OBJECT`، لكنه لا يستخدم signals/slots/qobject_cast. الإصلاح يُغيِّر سياق الترجمة بلا فائدة.
  - [x] تحذيرات `lupdate` الإضافية: `supplier_dialog.cpp:127` و`supplier_return_dialog.cpp:80`.

## 🟢 مؤجلة
- [ ] FIFO للدفعات.
- [ ] تقييم المخزون بسعر الحبة (لا العلبة) للزكاة — مُتفَق عليه.
- [ ] تطبيق الهاتف: قارئ فقط، لا كتابة.
- [ ] حذف `package_size` القديم بعد استقرار `pieces_per_package`.
- [ ] `mahali_en.ts` كـlocale حقيقي.
- [ ] LTR (fr/en) — هل جُرِّب فعلاً؟
- [ ] قناة الإصدار: GitHub Release تلقائي أم يدوي؟
- [ ] `apps/android/main.cpp`.
- [ ] الفاتورة المطبوعة (لا كود طباعة/PDF في المشروع).
- [ ] Code signing لـ`mahali-desktop.exe` (لتفادي حجب Chrome/Windows).

## ✅ مُنجَز

### الجلسات السابقة
- [x] Ajustement (adjustment_cents) end-to-end — `6d32a5d`.
- [x] Zakat overhaul + credit sale button.
- [x] fr + ar 606/606.

### هذه الجلسة
- [x] حقول الحزمة (pieces_per_package, package_name, package_barcode, package_cost_cents).
- [x] ترحيل 13 + 14 (backfill).
- [x] `pieces_consumed` في كل مسار المال.
- [x] `resolveSaleItems` واعية بالوحدة.
- [x] تقارير COGS على `piecesConsumed`.
- [x] POS: عمود Unité (قائمة منسدلة) + باركود العلبة.
- [x] حوار المنتج: اسم العلبة + عدد الحبات + الكمية الافتتاحية.
- [x] الكمية الافتتاحية تُكتب كحركة `opening`.
- [x] الحارس `reverse` يرفض `piecesConsumed <= 0`.
- [x] 16/16 اختبار أخضر.
- [x] ذاكرة المشروع (CONTEXT/TASKS/DECISIONS/COMMANDS).
- [x] `constantTimeEquals` لـ`verifyMaster` — commit `59bd395`.
- [x] تصنيف 1xx/3xx كـPermanent في `errorClassForStatus` — commit `59bd395`.
- [x] `.gitignore` يشمل `build.log`, `Testing/`, `translations/*.qm` — commit `59bd395`.
- [x] ترجمة عربية لسلسلة "chiffrer" — commit `59bd395`.
- [x] تحذير COGS في صفحة التقارير — commit `bd13833`.
- [x] D25 في `DECISIONS.md` — commit `34f9c47`.