# DECISIONS — سجل القرارات

## D01. المال = long long سنتات
ممنوع floats. الفائض يُفحَص (`productFits`, `sumFits`) قبل التنفيذ.

## D02. core لا يعتمد على Qt GUI
مفروض بـlink graph. `i18n.cpp` و`zakat_notifier.cpp` في core/ لكن يُترجمان في data/.

## D03. كل كتابة مالية داخل transaction + خدمة
الخدمة تكتب `audit_log`. على الجهاز: transaction واحدة مع opId جديد وصف `sync_outbox`.

## D04. الجداول المحاسبية append-only
العكس = صف سالب معكوس. `active=0` بدل الحذف. عكس واحد لكل أصل.

## D05. ترتيب مُنشئ Database
`applyPragmas` → `createSchema` → `createSingleOpenSessionIndex` → migrate helpers → `runSchemaMigrations`.

## D06. الترحيلات idempotent وغير قاتلة
فحص `PRAGMA table_info` قبل ALTER. `runSchemaMigrations` يختم `user_version` بلا شرط (مقصود).

## D07. `adjustment_cents` يُضاف كعمود أخير
`total = Σlines + adjustment`. COGS لا يتأثر. العكس يقلب الإشارة.

## D08. المزامنة idempotent عبر `applied_ops`
كل عملية في transaction مستقلة. السيرفر يعيد اشتقاق costPrice.

## D09. `SyncOpType` = { Sale, CustomerDebt, CustomerPayment }
العكس لا يُزامَن. موثق في migration 4.

## D10. دفعات المورد لا تعيد حساب PMP
"نقطة للنقاش لاحقاً".

## D11. صفر علامات TODO/FIXME/HACK
النية تُحمَل في نثر التعليقات.

## D12. الترجمات تُبنى في `build/`
`lrelease` من CMake. `.qm` بجانب `.ts` غير مقصود.

## D13. `lupdate` على كل المصادر
`ui data core apps`. حذف `apps` مع `-no-obsolete` يمسح مدخلات حية.

## D14. تطبيق الهاتف = قارئ فقط
لا كتابة، لا مزامنة صادرة. بعد اكتمال تطبيق الحاسوب.

## D15. `resolveSaleItems()` هي المصدر الوحيد
تملأ `unitKind` و`piecesConsumed`. ممنوع مسار يكتب صفوف `sale_items` بدون المرور بها.

## D16. `reverse` يرفض `piecesConsumed <= 0`
لا تصحيح صامت، لا عكس صامت.

## D17. `pieces_consumed` مقدار موجب دائماً
الإشارة في `quantity`. أي دالة تُجمع صفوفاً من الدفتر تأخذ المقدار من `pieces_consumed` والإشارة من `quantity`.

## D18. التقارير تتخطى وتُحذّر؛ البيع الحيّ يرفض
`cogsFor` (تقارير) يعمل على بيانات مسجَّلة → `continue + qWarning`.
`cogsCentsFor` (بيع حيّ) يرفض → `optional`.
عدم التماثل مقصود.

## D19. اختبار الحارس يُثبت بحذف الحارس
قبل تسليم اختبار فائض: احذف الحارس، أعد البناء، أكّد أن الاختبار يفشل، ثم أعده.

## D20. `QMessageBox::question` في الاختبارات
استهدف `QApplication::activeModalWidget()`. لا تستخدم `findChild<QMessageBox*>` (لن يجده).
إن كان هناك أكثر من modal في نفس المسار (نموذج + تأكيد)، استخدم polling مع phase variable.

## D21. سعر العلبة محسوب دائماً
= سعر الحبة × N. لا يُخزَّن منفصلاً. الخصم على العلبة يُطبَّق عبر `adjustment`.

## D22. سعر تكلفة العلبة مخزَّن منفصلاً
لتجنّب خسارة الكسور. `package_cost_cents` يُملأ عند الشراء.

## D23. الزكاة تُقيّم المخزون بسعر بيع الحبة
الخيار الأحوط.

## D24. `package_size` القديم يبقى
الحوار يكتبه مع `pieces_per_package` من نفس الحقل. لا قارئ إنتاجي له.