# TASKS — قائمة العمل

## 🔴 حرجة
- [ ] **Ajustement + المزامنة**: `SyncOperation` لا يحمل `adjustment_cents`، و`SyncProcessor::applySale` لا يمرّرها. بيع من جهاز مع adjustment يُزامن الإجمالي ويفقد العمود على السيرفر.
- [ ] **مفتاح HMAC افتراضي ثابت** في `apps/desktop/main.cpp:72`. المحلات التي لم تفتح الإعدادات تشترك في نفس المفتاح.
- [ ] **`apps/android/main.cpp` مفقود**. `-DMAHALI_ANDROID=ON` مكسور.

## 🟡 مهمة
- [ ] تقرير COGS في `report_service.cpp` يتخطى الصفوف ذات الفائض بـ`qWarning` بدل الرفض. يجب أن يُعلَّم التقرير "غير مكتمل" عند التخطي.
- [ ] `Q_OBJECT` مفقود في `purchase_dialog.cpp:104` و`select_customer_dialog.cpp:58` و`mini_bar_chart.cpp:19`.
- [ ] `refreshTotals()` في POS يسجّل "Unités: N" بـ`line.quantity`. الكرتونة تُحسب 1 لا 24. قرار UX.
- [ ] `Impossible de chiffrer « %1 »` — سلسلة فرنسية داخل كود عربي. مراجعة مستقبلية.
- [ ] `sync_hmac_key` الافتراضي يُغيَّر عند أول تشغيل.
- [ ] `SyncProtocol::errorClassForStatus` يعيد `Network` لـ3xx/1xx (redirect loop محتمل).
- [ ] `13cba43` "WIP: audit fixes (unverified)" — يستحق مراجعة.
- [ ] `.vscode/launch.json` يشغّل Chrome — مخلّف.

## 🟢 مؤجلة
- [ ] FIFO للدفعات.
- [ ] تقييم المخزون بسعر الحبة (لا العلبة) للزكاة — مُتفَق عليه.
- [ ] تطبيق الهاتف: قارئ فقط، لا كتابة.
- [ ] حذف `package_size` القديم بعد استقرار `pieces_per_package`.
- [ ] `mahali_en.ts` كـlocale حقيقي.
- [ ] LTR (fr/en) — هل جُرِّب فعلاً؟
- [ ] قناة الإصدار: GitHub Release تلقائي أم يدوي؟
- [ ] `apps/android/main.cpp`.

## ✅ مُنجَز
- [x] Ajustement (adjustment_cents) end-to-end — `6d32a5d`.
- [x] Zakat overhaul + credit sale button.
- [x] fr + ar 606/606.
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