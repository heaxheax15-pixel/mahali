# COMMANDS

## بناء
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

## تشغيل
./build/apps/desktop/mahali-desktop
./build/apps/desktop/mahali-desktop /path/to/db.sqlite
# الافتراضي: ~/.local/share/mahali

## اختبار
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "^data$" -V
QT_QPA_PLATFORM=offscreen ./build/tests/tst_ui

## ترجمات
# lupdate — القائمة الكاملة
/usr/lib/qt6/bin/lupdate -recursive -extensions cpp,h -locations relative -no-obsolete \
  ui data core apps -ts translations/mahali_ar.ts translations/mahali_fr.ts
# lrelease يعمل تلقائياً في البناء

## أدوات
cmake -S . -B build -DMAHALI_TOOLS=ON       # + mahali-screenshot
python3 tools/seed_demo.py /tmp/mahali-demo/db.sqlite [dark]

## Windows
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
windeployqt --release build\mahali-desktop.exe
packaging\Installer.bat