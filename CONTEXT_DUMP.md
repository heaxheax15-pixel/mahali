# CONTEXT_DUMP.md — `mahali` (محلي)

Read-only context dump generated for handoff to another AI assistant.
Nothing in the repository was modified to produce this file (only this file was created).

---

## 1. Project overview

| Field | Value |
|---|---|
| **Name** | `mahali` — "محلي" (means "local" / "neighbourhood shop") |
| **Version** | `1.0.4` (`CMakeLists.txt:5`) |
| **Purpose** | Offline-first POS + inventory + accounting + LAN-sync system for a grocery/convenience store. Full Arabic RTL UI, cash sessions, credit sales, purchases, suppliers, expenses, zakat, refunds, audit log. |
| **Language** | C++17 (no JS/TS/Python except two dev `tools/` scripts) |
| **Framework** | Qt 6 — `Core`, `Sql`, `Network`, `Widgets`, `HttpServer`, `Gui` (private), `Test` |
| **Build** | CMake 3.16+ (using 3.28.3 locally), AUTOMOC/AUTORCC on, AUTOUIC **off** (see comment at `CMakeLists.txt:14-16`) |
| **DB** | SQLite via QtSql, WAL journal on the server side, DELETE journal on devices |
| **Platform targets** | Windows desktop (primary, MSVC CI), Linux desktop (dev), Android (scaffold only, `MAHALI_ANDROID=OFF`) |
| **Local toolchain** | Qt **6.4.2**, GCC 13.3.0, Ubuntu 24.04 |
| **CI Qt version** | Qt **6.5.3** MSVC 2019 64-bit, modules `qthttpserver qtwebsockets` |
| **i18n** | `translations/mahali_{ar,fr,en}.ts` → `.qm` embedded in the binary under `:/i18n/` |
| **Tests** | 16 CTest targets, 16/16 green at HEAD |
| **Code size** | core 1,993 · data 10,810 · network 1,063 · ui 13,602 · apps 571 · tests 10,725 lines |

### Current state
Clean tree on `main`, fully in sync with `origin/main`. The last commit
(`6d32a5d`) shipped the **Ajustement (invoice adjustment)** feature plus the
completion of the French/Arabic catalogues. Build is green, all tests pass.

---

## 2. Directory structure

```
/home/heax/mahali
├── CMakeLists.txt                  # top-level: project(), find_package(Qt6), enable_testing()
├── README.md                       # Arabic docs: requirements, Windows/Linux build, distribution
├── build.log                       # tracked leftover of a previous build (noise)
├── .gitignore                      # build/ build-*/ *.user *.suo .cache/ compile_commands.json
├── .github/workflows/build-windows.yml
├── .vscode/launch.json             # stale Chrome launcher, unrelated to this project
│
├── core/       (52 files)  app::core   — pure business logic, ZERO Qt GUI dependency
├── data/       (90 files)  app::data   — QtSql repositories + domain services + schema/migrations
├── network/    (13 files)  app::network— QHttpServer sync server, client, protocol, scheduler
├── ui/         (78 files)  app::ui     — QtWidgets pages, dialogs, widgets, themes, resources
├── apps/
│   ├── desktop/main.cpp            # THE entry point (112 lines)
│   ├── android/CMakeLists.txt      # scaffold only
│   └── screenshot/main.cpp         # screenshot renderer (MAHALI_TOOLS=ON)
├── tests/      (17 files)  tst_*.cpp  — QtTest, one binary per area
├── tools/      (2 files)   make_icon.py, seed_demo.py   (Python, stdlib + PIL)
├── packaging/  Installer.bat, CreateShortcut.ps1
├── docs/       FINANCIAL_MODEL.md, screenshots/, screenshots-dark/
├── translations/ mahali_ar.ts, mahali_fr.ts, mahali_en.ts
├── Testing/                        # tracked CTest leftovers (should probably be gitignored)
└── ui-polish-shots/                # tracked before/after UI screenshots, 7.9 MB
```

`ui-polish-shots/` is a before/after corpus from the UI-polish work:
`before/{1024x600,1366x768,1920x1080}/` (14 pages each) and
`after/{1024x600,1366x768,1920x1080,rtl}/` (15 pages each — `users.png` and the extra
`rtl/` set were added later). ~130 PNGs, all tracked in git.

File counts above include non-source assets. Exact breakdown:
- `core/` 52 files = 51 `.h`/`.cpp` + `CMakeLists.txt`
- `ui/` 78 files = 64 `.h`/`.cpp` (38 top-level + `dialogs/` 16 + `widgets/` 10)
  + `CMakeLists.txt` + `resources/` 13 (`app.qrc`, 2 Noto Sans Arabic fonts + `OFL.txt`,
  6 app PNGs at 32–512 px + `mahali.ico`, and `themes/{light,dark}.qss`)
- `tests/` 17 = 16 suites + `CMakeLists.txt`
- `apps/desktop/` also holds `mahali-green.ico`, `mahali-orange.ico`, `mahali.rc` (the `.rc`
  is what paints the taskbar icon on Windows; `main.cpp` sets the in-app icon)

The 19 page/dialog units at the top of `ui/`:
`pos_page`, `products_page`, `stock_page`, `customers_page`, `suppliers_page`,
`cash_session_page`, `sales_page`, `expenses_page`, `reports_page`, `refunds_page`,
`audit_log_page`, `settings_page`, `users_page`, plus `main_window`, `login_dialog`,
`server_controller`, `theme`, `quick_items_bar`, and the header-only
`scan_safe_dialog.h` / `theme_tokens.h`.

---

## 3. Dependency manifests

**There is no `package.json` / `requirements.txt` / `Cargo.toml` / `go.mod` etc.**
This is a pure CMake + Qt C++ project. The manifests are the `CMakeLists.txt` files.

### 3.1 `CMakeLists.txt` (root) — full contents

```cmake
cmake_minimum_required(VERSION 3.16)

project(mahali
    VERSION 1.0.4
    DESCRIPTION "POS + Inventory + Accounting + Sync (offline-first grocery)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# No Qt Designer (.ui) forms are used; AUTOUIC is disabled because its
# `ui_<name>.h` pattern would misclassify plain headers like "ui_helpers.h".
set(CMAKE_AUTOUIC OFF)

option(MAHALI_ANDROID "Build the Android POS target (requires Qt for Android toolchain)" OFF)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Sql Network Widgets HttpServer Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql Network HttpServer Test)

# Translation catalogues. Consumed by apps/desktop/CMakeLists.txt, which
# compiles them with lrelease and embeds the resulting .qm under /i18n.
set(TS_FILES
    translations/mahali_ar.ts
    translations/mahali_fr.ts
    translations/mahali_en.ts)

# Qt6LinguistTools (and therefore qt_add_translations) is not shipped by every
# Qt6 package set, so resolve the lrelease tool directly and fall back to the
# classic add_custom_command pipeline in apps/desktop.
find_program(MAHALI_LRELEASE
    NAMES lrelease-qt6 lrelease6 lrelease
    HINTS
        ${QT6_INSTALL_PREFIX}/libexec
        ${QT6_INSTALL_PREFIX}/bin
        /usr/lib/qt6/bin
        /usr/lib/qt6/libexec
    DOC "Qt lrelease tool used to compile the .ts catalogues")
if(NOT MAHALI_LRELEASE)
    message(FATAL_ERROR
        "lrelease not found. Install qt6-l10n-tools (or the Qt LinguistTools "
        "development package) to build mahali-desktop.")
endif()
message(STATUS "mahali: lrelease = ${MAHALI_LRELEASE}")

add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(network)
add_subdirectory(ui)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)
```

### 3.2 Build target graph

```
mahali-core   STATIC  → Qt6::Core, Qt6::Network   (MAHALI_VERSION define, PUBLIC)
mahali-data   STATIC  → mahali-core, Qt6::Sql, Qt6::Network, Qt6::Gui(PRIVATE)
mahali-network STATIC → mahali-core, mahali-data, Qt6::Network, Qt6::HttpServer
mahali-ui     STATIC  → mahali-core, mahali-data, mahali-network, Qt6::Widgets
mahali-desktop EXE    → mahali-ui, Qt6::Widgets  (qt_add_executable; WIN32_EXECUTABLE on Windows)
mahali-screenshot EXE → mahali-ui  (only when -DMAHALI_TOOLS=ON)
mahali-android EXE    → mahali-core/data/network (only when -DMAHALI_ANDROID=ON)
```

All four static libs set `POSITION_INDEPENDENT_CODE ON`.

**Architectural rule (enforced by the link graph):** `app::core` must never depend on
Qt GUI. `core/i18n.cpp` and `core/zakat_notifier.cpp` are physically in `core/` but
**compiled into `mahali-data`** because they read `SettingRepository` (data depends on
core, not the reverse).

### 3.3 Options / caches
- `MAHALI_ANDROID` (default OFF) — Android target
- `MAHALI_TOOLS` (default OFF, `apps/CMakeLists.txt:3`) — screenshot renderer
- `CMAKE_BUILD_TYPE`, `MAHALI_LRELEASE` (auto-discovered)

---

## 4. Scripts and commands

There is **no Makefile, justfile, npm-style scripts, or task runner**. All commands
come from CMake/CTest and the README.

```bash
# ---- configure & build (Linux, local dev) ----
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --build build -j8

# ---- run ----
./build/apps/desktop/mahali-desktop                    # data in ~/.local/share/mahali
./build/apps/desktop/mahali-desktop /path/to/db.sqlite  # explicit DB path

# ---- test ----
ctest --test-dir build --output-on-failure              # 16 tests
ctest --test-dir build -R "^data$" -V                  # one suite, verbose
QT_QPA_PLATFORM=offscreen ./build/tests/tst_ui         # UI suite headless (no xvfb needed)

# ---- translations ----
/usr/lib/qt6/bin/lupdate -recursive -extensions cpp,h -locations relative -no-obsolete \
    ui data core apps -ts translations/mahali_fr.ts translations/mahali_ar.ts
/usr/lib/qt6/bin/lrelease translations/mahali_fr.ts
# lrelease is ALSO run automatically by the build (apps/desktop/CMakeLists.txt), so a
# malformed .ts breaks the build. lrelease writes .qm next to the .ts when run bare.

# ---- optional targets ----
cmake -S . -B build -DMAHALI_TOOLS=ON      # + mahali-screenshot
cmake -S . -B build -DMAHALI_ANDROID=ON    # Android (needs an Android Qt toolchain)

# ---- dev tools (Python 3, stdlib; make_icon.py needs Pillow) ----
python3 tools/seed_demo.py /tmp/mahali-demo/db.sqlite [dark]
python3 tools/make_icon.py

# ---- Windows ----
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
windeployqt --release build\mahali-desktop.exe
packaging\Installer.bat   # copies to C:\mahali, creates shortcut, launches
```

**No linter, formatter, or type-checker is configured** (no `.clang-format`,
no `.clang-tidy`, no `cppcheck` step). Compiler warnings are not escalated to errors.

---

## 5. Git status

```
$ git status
On branch main
Your branch is up to date with 'origin/main'.

nothing to commit, working tree clean

$ git branch --show-current
main

$ git diff --stat           # empty
$ git diff --cached --stat  # empty

$ git log --oneline -20
6d32a5d feat(products): delete unused, disable sold; i18n updates
3c2c911 feat(products): delete products, disable sold ones
e4b1e0e i18n: complete French and Arabic translation (586/586)
87df98a feat: zakat overhaul + credit sale button (phases A, B, C)
826c413 feat(zakat): fix notifier + history table + Reports log
e5ada80 v1.0.4
828d11f chore: bump version to 1.0.4
5c19f90 UI MODIFAIDE
378ba01 Polish UI across pages and themes
4f779ae Fix cash reference test, remove stock trigger; 16/16 green
13cba43 WIP: audit fixes (unverified)
9d72f08 feat(refunds): Total/Montant column stretches, fixed columns around it
d1e5be8 fix(ui): QHeaderView background for dead space in tables
227e060 feat(ui): compact top bar + column width rebalance
5394ba3 chore: bump version to 1.0.3
0bb4176 feat(ux): auto-focus POS entry, F11 fullscreen, showMaximized
d69c20b feat(products): hide nameless products from lists (scanner still finds them)
f77744a feat(product-dialog): warn when a barcode matches a nameless product
e1ff3ae feat(topbar): 4 quick nav buttons for Vente, Produits, Stock, Rapports
fd12db7 chore: bump version to 1.0.2

$ git log -1 --format="%H %an %ae %ad"
6d32a5d3d85250ddc171d8d9ec6c165a9093614e  heax23  Sun Oct 4 10:28:45 2026 +0100
                                                                              (email withheld from this dump)
20 commits shown, all by one author (heax23), 0 merge commits. 101 commits total on main.
```

**Uncommitted changes: none.** The Ajustement work described in earlier turns is
already committed as `6d32a5d` (14 files, +660/−64).

**Tracked build artefacts that should probably be gitignored:**
`build.log`, `Testing/Temporary/CTestCostData.txt`, `Testing/Temporary/LastTest.log`.

---

## 6. Configuration and environment

### 6.1 No `.env` files, no secrets files, no `docker-compose`, no `Dockerfile`

Searched: `.env*`, `*.pem`, `*.key`, `*secret*` — the only hits are
`data/admin_secret_repository.{h,cpp}`, which is *code* for password hashing, not a secret store.

### 6.2 Secrets and authentication — findings

| What | Where | Notes |
|---|---|---|
| Default sync HMAC key | `apps/desktop/main.cpp:72` | Falls back to a **hardcoded literal** (`[REDACTED]`) when the `sync_hmac_key` setting is absent. Overridable from the Settings page. **Security smell**: every shop that never set the key shares the same signing key. |
| HMAC verification | `network/sync_server.cpp:21-25` | `X-Mahali-Signature` header, hex-encoded HMAC-SHA256 over the request body; verified in `SyncProcessor::process`. |
| Admin master password | `data/admin_secret_repository.cpp` | 16-byte random salt + `QPasswordDigestor::deriveKeyPbkdf2(SHA256, pw, salt, 100000 iters, 32 bytes)`, stored hex in `admin_secrets`. Comparison is a plain `==` on hex strings (not constant-time). |
| User PINs | `data/user_repository.cpp` | **Stored in PLAINTEXT.** `kUserColumns = "id, name, role, pin, active"`, `findByPin()` runs `SELECT ... WHERE pin = ? AND active = 1` (direct string match, no hashing), and `savePin()` runs `UPDATE users SET pin = ?`. Contrast with the admin master password, which *is* PBKDF2-hashed. See 9.2 #13. |
| Device auth token | `core/device.h:11` | `QString authToken` — a struct field only; the DB column is `devices.auth_token TEXT NOT NULL` (`database.cpp`). No value hardcoded anywhere in the tree. |
| CI secrets | `.github/workflows/build-windows.yml` | **None used.** No tokens, no registry push. Build + `upload-artifact` only. |

### 6.3 Runtime configuration — the `settings` key/value table

All app configuration lives in the SQLite `settings` table (`core/setting.h`,
`data/setting_repository.cpp`). Keys in use:

```
language            ("ar" | "fr" | "en")   — defaultLanguage(); persisted on first launch
theme               ("light" | "dark")     — read before the window exists
currency_symbol     e.g. "DA"              — feed into core::format_utils' live symbol
shop_name
sync_hmac_key       [REDACTED — see 6.2]
device_id           the device's own sync identity (written by DeviceLedgerService)
zakat_date          see core/zakat_notifier.cpp
zakat_paid_date     when zakat was last paid for the year (see zakat_history)
active_occasion_id  current occasion (nullable id)
sidebar_visible     shell state (see main_window.cpp)
```

Verified by grepping every `QStringLiteral("...")` that reaches the settings repository.

### 6.4 `.github/workflows/build-windows.yml` — full contents

```yaml
name: Build Windows

on:
  push:
    branches: [ main, master ]
  workflow_dispatch:

jobs:
  build-msvc:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.5.3'
          arch: win64_msvc2019_64
          modules: 'qthttpserver qtwebsockets'
          setup-python: 'false'
          cache: true
      - name: Configure
        run: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
      - name: Build
        run: cmake --build build --config Release --target mahali-desktop
      - name: Deploy
        shell: pwsh
        run: |
          mkdir dist
          $exe = Get-ChildItem -Recurse -Filter "mahali-desktop.exe" build | Select-Object -First 1
          Copy-Item $exe.FullName dist\
          windeployqt --release "dist\mahali-desktop.exe"
          copy apps\desktop\mahali-green.ico dist\
          copy packaging\Installer.bat dist\
          copy packaging\CreateShortcut.ps1 dist\
      - name: Zip
        run: Compress-Archive -Path dist\* -DestinationPath Mahali-Setup.zip
      - uses: actions/upload-artifact@v4
        with:
          name: mahali-msvc
          path: Mahali-Setup.zip
```

Note: the CI builds only `--target mahali-desktop`, so **tests are never run in CI**.

### 6.5 `.gitignore` — full contents

```
build/
build-*/
*.user
*.suo
.cache/
compile_commands.json
```

---

## 7. Key source files

### 7.1 Entry points

| Path | Purpose |
|---|---|
| `apps/desktop/main.cpp` (112 lines) | **The** production entry point. `Q_INIT_RESOURCE(app)` → `QApplication` → resolve DB path (argv[1] or `QStandardPaths::AppDataLocation`) → `Database(path, DatabaseMode::Server)` → read HMAC key + language + theme → `LoginDialog::exec()` (blocking gate) → `ServerController::start()` → `MainWindow` → `showMaximized()` → `app.exec()`. Every step carries a paragraph comment explaining *why it is ordered this way* (e.g. the login gate runs before the server starts so fourteen pages are not laid out behind an open dialog; `showMaximized()` happens before first paint so the shell is never seen at minimum size and grown). |
| `apps/screenshot/main.cpp` | Renders one PNG per page in the stored theme (`-DMAHALI_TOOLS=ON`). |
| `apps/android/main.cpp` | **Does not exist** — `apps/android/CMakeLists.txt` references it but the file is absent; the target is only configured when `MAHALI_ANDROID=ON`, so the tree still configures. |
| `ui/main_window.cpp` (1380 lines) | The shell: top bar, grouped right-hand sidebar, `QStackedWidget` of lazily built pages, sync status line, theme/sidebar toggles, update bar. |

`apps/desktop/main.cpp` — key excerpt (auth/redaction applied):

```cpp
int main(int argc, char* argv[])
{
    Q_INIT_RESOURCE(app);                      // app.qrc lives in libmahali-ui.a; without
                                               // this the linker drops qrc_app.o
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QCoreApplication::translate("main", "محلي"));
    app.setOrganizationName(QStringLiteral("mahali"));
    app.setApplicationVersion(QStringLiteral(MAHALI_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/mahali/icons/app.ico")));
    app.setAttribute(Qt::AA_DontShowIconsInMenus, true);

    QString dbPath;
    if (argc > 1) dbPath = QString::fromLocal8Bit(argv[1]);
    else {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        dbPath = QDir(dataDir).filePath(QStringLiteral("mahali.sqlite"));
    }

    std::unique_ptr<app::data::Database> db;
    try {
        db = std::make_unique<app::data::Database>(dbPath, app::data::DatabaseMode::Server);
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("main", "محلي — خطأ"),
            QCoreApplication::translate("main", "تعذر فتح قاعدة البيانات:\n%1")
                .arg(QString::fromUtf8(e.what())));
        return 1;
    }

    // The sync key is set once in Settings (Phase 14); default for first launch.
    app::data::SettingRepository settings(*db);
    const QByteArray hmacKey = settings.value(QStringLiteral("sync_hmac_key"))
                                   .value_or(QStringLiteral("[REDACTED — hardcoded default literal]"))
                                   .toUtf8();

    if (!settings.value(QStringLiteral("language")).has_value())
        settings.set(QStringLiteral("language"), app::core::defaultLanguage());
    app::core::applyLanguage(app::core::currentLanguage(*db));
    app::ui::setCurrencySymbol(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));
    app::ui::applyTheme(settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light")), app);

    app::data::UserRepository userRepo(*db);
    app::ui::LoginDialog login(*db);
    if (login.exec() != QDialog::Accepted) return 0;

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();
    app::ui::MainWindow window(*db, controller);
    window.showMaximized();
    return app.exec();
}
```

### 7.2 Core layer — `core/` (business logic, no GUI)

| File | Contents |
|---|---|
| `core/sale.h` | `struct Sale { id, createdAt, totalCents, **adjustmentCents**, deviceId, oversold, reversedSaleId, optional<int> occasionId }`. The `adjustmentCents` comment states the invariant outright: *"total_cents is always the sum of the sale's items plus this, and it holds for a reversal too"*, and gives the reason it is not folded into the total — "a figure that has been edited without leaving a trace of what was edited is a figure nobody can check afterwards". |
| `core/customer_transaction.h` | `struct CustomerTransaction { id, customerId, amountCents, **adjustmentCents**, createdAt, reversedTransactionId }` |
| `core/sale_item.h`, `customer_transaction_item.h` | `{ id, <parent>Id, productId, quantity, unitPriceCents, unitCostCents, reversedId }` — price *and* cost frozen onto the line |
| `core/product.h` | `{ id, barcode, name, costPriceCents, salePriceCents, quantity, unit, packageSize, active, soldByWeight }` |
| `core/sync_operation.h` | `enum class SyncOpType { Sale, CustomerDebt, CustomerPayment }`, `SyncOperation`, `SyncItem` |
| `core/applied_op.h` | `SyncApplyToken { opId, deviceId }` — the exactly-once idempotency key |
| `core/format_utils.h` | `formatMoney(cents)`, `parseMoney(text) -> optional<long long>` (accepts `12`, `12.5`, `12,50`, `1,234.50`, `500 دج`, Arabic-Indic digits; **rejects negatives**), live currency symbol cache |
| `core/barcode_utils.{h,cpp}` | `normalizeScannedBarcode()` — AZERTY-aware: remaps the unshifted AZERTY number row `&é"'(-è_çà` so a barcode typed on a French keyboard still matches |
| `core/zakat_calculator.{h,cpp}` | zakat base + 2.5% |
| `core/profit_loss_calculator.{h,cpp}` | P&L arithmetic |
| `core/cash_session_calculator.{h,cpp}` | expected cash vs counted |
| `core/sync_operation_codec.{h,cpp}` | JSON (de)serialisation of `SyncOperation` |
| `core/version.{h,cpp}` | `Version{major,minor,patch}`, `isNewer`, `parseVersion` |
| `core/update_checker.{h,cpp}` / `update_downloader` / `update_installer` | GitHub-releases-based self-update (needs Qt Network — allowed by the "no GUI" rule) |
| `core/session.{h,cpp}` | singleton holding the signed-in `User`, `actorName()` for the audit log |
| `core/i18n.h` (+ `.cpp` in **data** target) | `applyLanguage(code)`, `currentLanguage(db)`, `defaultLanguage()`, `supportedLanguages()` |
| `core/zakat_notifier.h` (+ `.cpp` in **data** target) | `shouldNotifyZakat()` — one annual reminder |

### 7.3 Data layer — `data/` (repositories + services)

**`data/database.cpp` (902 lines)** — `class Database`, one `QSqlDatabase` per instance
(unique connection name `mahali_<uuid>`), non-copyable. Modes:
- `DatabaseMode::Server` → `journal_mode=WAL`, `synchronous=FULL`, `foreign_keys=ON`, `busy_timeout=5000`
- `DatabaseMode::Device` → `journal_mode=DELETE` (no companion files on the phone)

Constructor order (`database.cpp:316-332`):
`applyPragmas()` → `createSchema()` → `createSingleOpenSessionIndex()` →
7 unversioned `migrate*Table()` helpers → `runSchemaMigrations()`.

Error discipline: `recordError(QSqlError, context)` is called by every repository on a
failed `exec()`; `clearError()` resets both message and context at the start of each
operation so `lastError()` never reports a stale failure.

**28 tables** (from `createSchema()`):
```
products, sales, occasions, sale_items, customers, customer_transactions,
customer_transaction_items, suppliers, payments, expenses, owner_drawings,
stock_movements, cash_sessions, cash_movements, users, admin_secrets, devices,
audit_log, settings, zakat_history, sync_outbox, applied_ops, sync_sequence,
purchases, purchase_items, supplier_payments, supplier_returns, supplier_return_items
```
Plus **1 trigger** (`trg_stock_after_insert`) and these index groups:

| Group | Count | Where created |
|---|---|---|
| Base per-table indexes | 13 | `createSchema()` in `database.cpp` |
| One-open-cash-session | 1 | `Database::createSingleOpenSessionIndex()` — `CREATE UNIQUE INDEX idx_cash_sessions_one_open ON cash_sessions(status) WHERE status = 'open'` (`database.cpp:821`) |
| One-reversal-per-original (partial unique) | 5 | **migration 4** (`kReversalIndexes`, `schema_migrations.cpp:135`) |
| Report range-scan indexes | 15 | **migration 8** (`kReportIndexes`, `schema_migrations.cpp:212`) |

So a fully-migrated database carries **34 indexes**, but only 14 of them are in
`createSchema()`. The other 20 arrive via migrations — including on a *fresh* install,
because `PRAGMA user_version` is 0 on a brand-new file, so `runSchemaMigrations()` runs
every entry. (The `Database` constructor comment claiming a fresh install "has nothing to
replay" is therefore imprecise: the tables come from `createSchema()`, but the index work
still runs through migrations 4 and 8.)

The 5 reversal indexes are `uq_payments_one_reversal`, `uq_expenses_one_reversal`,
`uq_owner_drawings_one_reversal`, `uq_sales_one_reversal`,
`uq_customer_transactions_one_reversal`, all `UNIQUE(col) WHERE col <> 0`. Migration 4
**refuses to create one when the table already holds duplicate reversals** and logs a
`qWarning` naming the offending ids and saying the database is *not* protecting the ledger,
only the service checks are.

**`data/schema_migrations.{h,cpp}`** — `constexpr int kSchemaVersion = 12`.
Table `kMigrations[]` of `{version, fn}`; **versions 2..12** (there is no 1). Each migration
is idempotent — it inspects `PRAGMA table_info` before altering. `runSchemaMigrations()`
returns early when `from >= kSchemaVersion`, otherwise it skips versions `<= from` or
`> kSchemaVersion`, runs the rest, then **stamps `PRAGMA user_version` unconditionally**,
even when a migration only warned (so the warning does not repeat on every launch).
Deliberately non-fatal.

Migration 12 (most recent, `migrateSaleAdjustment`, `schema_migrations.cpp:394`) adds
`sales.adjustment_cents` and `customer_transactions.adjustment_cents`, both
`INTEGER NOT NULL DEFAULT 0`, guarded by a `tableColumns(...).contains(...)` check.

**`data/sale_service.{h,cpp}`** — the money path. The two recording entry points both end
in a defaulted adjustment parameter:

```cpp
SaleRecordResult recordSale(const QVector<core::SaleItem>& items, int cashSessionId,
                            const QString& deviceId, bool allowOversold,
                            const core::SyncApplyToken* applyToken = nullptr,
                            long long adjustmentCents = 0);

SaleRecordResult recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items,
                                    const QString& deviceId, bool allowOversold,
                                    const core::SyncApplyToken* applyToken = nullptr,
                                    long long adjustmentCents = 0);

SaleReverseResult reverseSale(int saleId, int cashSessionId);
SaleReverseResult reverseCustomerDebt(int transactionId);   // deliberately no session

// idempotency plumbing, used by SyncProcessor:
bool insertAppliedOp(const core::SyncApplyToken& token, core::SyncOpType opType,
                     int entityId, ...);
SaleRecordResult alreadyAppliedResult(const core::AppliedOpRecord& record);
```

`SaleRecordResult { ok, alreadyApplied, saleId, totalCents, cogsCents, error }`.
Invariants enforced here:
- `total = Σ(unitPrice×qty) + adjustmentCents`, every step gated by `detail::sumFits()`
  and refused rather than truncated
- `cogs  = Σ(unitCost×qty)` — **never** touched by the adjustment
- reversal rows mirror the adjustment with a flipped sign, so
  `total == Σlines + adjustment` holds for mirrored rows too
- `reverseSale` re-checks `SELECT 1 FROM sales WHERE reversed_sale_id = ?` **inside** the
  transaction before writing, so the uniqueness holds even if the index is absent

**`data/sale_rules.h`** (header-only, all `inline`) — shared by the desktop service *and*
the device ledger so the two machines can never diverge on money math:
`resolveSaleItems()` (fills price/cost from the product, enforces stock unless
`allowOversold`), `totalCentsFor()` and `cogsCentsFor()` (both return
`optional<long long>`, **nullopt on overflow**), `insertSaleStockMovements()`, plus the
overflow guards `detail::productFits()` / `detail::sumFits()`. Those two are checked
rather than performed, and the comment explains the asymmetry they handle: the negative
range reaches one *further* than the positive one, so the same product can be too large to
store depending only on the sign — "a cancellation is exactly the case that needs this to
be right, its lines are negative and perfectly ordinary". The overflow is not theoretical:
a sold-by-weight line is typed in kilograms, so `quantity` is a decimal rather than a
count, and the header states the rule plainly — **"callers must refuse the sale rather
than record a number."**

**`data/device_ledger_service.{h,cpp}`** — the offline device side. Every financial write
commits **in one transaction** together with a freshly minted monotonic `opId` and the
`sync_outbox` row, so a power cut can never leave money recorded with no sync record.
Refuses everything if the device id is empty.

**Other services:** `payment_service`, `purchase_service`, `supplier_payment_service`,
`supplier_return_service`, `cash_entry_service` (expenses + owner drawings),
`cash_drawer.{h,cpp}` (records money in/out against a session with `ref_type`/`ref_id`),
`report_service` (`StoreReport`, P&L + zakat base + cash breakdown),
`daily_report_service`, `supplier_report_service`, `occasion_service`.

`data/cleanup_invalid_barcodes.sql` — a standalone maintenance script (not run by CMake).

### 7.4 Network layer — `network/` (1063 lines)

| File | Purpose |
|---|---|
| `sync_server.{h,cpp}` | `QHttpServer` with exactly **two** routes. `POST /api/sync` (`:20`) reads `X-Mahali-Signature`, hex-decodes it, and hands body+signature+key to `SyncProcessor`; returns `{ok, error, applied[], errors[]}` where each ack carries `opId`, `type`, `entityId`, `totalCents`, `cogsCents` and `alreadyApplied`. `GET /api/health` (`:79`) is the liveness probe the phone calls before sending batches — `{status:"ok", time:<ISO UTC ms>}`. |
| `sync_processor.{h,cpp}` | Applies a batch. Each op in its **own** transaction. Re-derives `costPrice` from the DB (never trusts the payload). Idempotency via `applied_ops` + `SyncApplyToken`. |
| `sync_client.{h,cpp}` | Device side of the HTTP conversation. |
| `sync_protocol.{h,cpp}` | `hmacSha256()`, `serializeOp`/`deserializeOp`, `batchSizeValid()` (1..50), `errorClassForStatus()` (4xx permanent / 5xx retry / 2xx ok / else network). |
| `sync_coordinator.{h,cpp}` | Drives client + scheduler, surfaces stats. |
| `sync_scheduler.{h,cpp}` | Retries the outbox on a timer. |

### 7.5 UI layer — `ui/` (13602 lines)

**`ui/pos_page.cpp` (1016 lines)** — the till, keyboard-first. Layout is a horizontal
`QSplitter` (`posWorkspace`, stretch 60/40, right pane `setMinimumWidth(400)`):

```
m_creditBar        (customer being charged, shown only in credit mode)
m_entry            the scan field — the only control above the panes
├─ cartPane  (60%) m_table "posTable" — THE SALE ITSELF
│                 columns: Produit | Code-barres | Unité | Qté | Prix
│                 NameColumn stretches, the rest Fixed; only Qté accepts a typed
│                 edit, and only via EditKeyPressed (F2 / typing)
└─ rightPane (40%) invoiceBar (objectName, min height 220)
                  totals + paymentSummary side by side, save button, then
                  remove / clear row (both 44 px tall)
```

Note the inversion, which the code comments explain at length: this table used to be the
**product catalogue**, and the cart used to be boxed into a narrow two-column strip on the
right. The lines need the room more than browsing does, so the grid took the wide slot and
the stock column it carried was replaced by the line quantity.

Key members and invariants:
- `m_lines` (`QVector<PosLine>`) is the single source of truth; the table is a projection
  rebuilt by `rebuildTable()` under an `m_updating` guard
- `PosPage::totalCents()` = Σlines **+ adjustment** (what will be recorded)
- `adjustmentFromField()` — parses `+`/`-` signs manually because `parseMoney` rejects
  negatives; returns `nullopt` for junk. The parsed value is cached in `m_adjustmentCents`
  so repainting the grid does not re-parse the field
- The adjustment renders as a **ghost row** appended to the grid only when ≠ 0
  (`pos_page.cpp:823-843`), labelled `tr("Ajustement")` with a `+ ` prefix when positive.
  It has no `productId`, no barcode, no quantity. Its row index is `>= m_lines.size()`,
  which every edit path checks (`row >= 0 || row >= m_lines.size()` at lines 668 and 704),
  so `onRemoveLine` skips it and `syncFromTable` tolerates `rowCount == size() + 1`
- `completeSale()` refuses the sale on unparsable adjustment text
  (`setNotice(tr("Ajustement invalide"), false)`, `pos_page.cpp:925`), passes the value to
  `recordSale` / `recordCustomerDebt` as the 6th argument (`pos_page.cpp:973`, `997`),
  and clears the field on success
- Credit mode: `enterCreditMode()` / `clearCreditMode()` / `isInCreditMode()`
- Test accessors: `lineCount`, `totalCents`, `adjustmentCents`, `lineQuantityAt`,
  `linePriceAt`, `lastSaleId`, `noticeText`, `setEntryText`, `setAdjustmentText`,
  `table()`, `entryField()`, `adjustmentField()`, `quickItemsBar()`

**`ui/main_window.h/.cpp`** — page indices are a `namespace page` of `constexpr int`
(`QuickSale=0 … Stock=14`) that double as `QStackedWidget` indices. Pages are built
lazily through `PageFactory{ create, refresh, instance }`. Sidebar groups
(`ui/main_window.cpp:934-963`):

```
نقطة البيع  : البيع السريع, جلسة الصندوق, مبيعات اليوم
الإدارة     : المنتجات, العملاء, الموردون, Achats, المناسبات
المالية     : المصاريف والسحوبات, التقارير, الاستردادات
النظام      : سجل المراجعة [+ إدارة المستخدمين (admin only)]
```

**Theme/i18n plumbing:** `ui/theme.{h,cpp}` reads `:/mahali/themes/<key>.qss`
(`light.qss`, `dark.qss`, both tracked) and applies it. Icons are drawn in code by
`ui/widgets/app_icon.cpp` (`Icon` enum + `appIcon()`), because pixmaps cannot be
recoloured by a stylesheet — `MainWindow::refreshThemeIcons()` repaints them on theme
change. Fonts bundled: `ui/resources/fonts/NotoSansArabic-{Regular,Bold}.ttf`.

**`ui/server_controller.{h,cpp}`** — headless owner of the sync server + stats polling,
deliberately UI-free so it is unit-testable without a display.

### 7.6 Documentation files

| Path | Contents |
|---|---|
| `README.md` | Arabic. Features, screenshots, Qt 6.4+ requirements, Windows (aqtinstall / MinGW / Ninja) and Linux build, `windeployqt`, tools, tests. **Says "10 اختبارات" — stale, there are 16.** |
| `docs/FINANCIAL_MODEL.md` (211 lines) | The accounting spec: integer cents only, `AUTOINCREMENT` ids, ISO-UTC `created_at`, prefer `active=0` over deletion, every write in a transaction. Then per-entity field tables, the purchase/supplier-payment/supplier-return/sale/credit-sale/cancellation business rules, and the reports. **The authority to consult before changing money arithmetic.** |

---

## 8. Tests

**Framework:** QtTest (`Qt6::Test`), one `QTest`-derived `QObject` per file, wired to
CTest via `add_test`. Each suite builds its own throwaway SQLite files under a
`QTemporaryDir` — several fixtures deliberately avoid the shared DB so counts are
relative, not absolute.

| # | ctest name | Binary | Lines | Links |
|---|---|---|---|---|
| 1 | `smoke` | `tst_smoke.cpp` | 16 | core+data+network+ui |
| 2 | `data` | `tst_data.cpp` | **4223** | data |
| 3 | `purchase_service` | `tst_purchase_service.cpp` | 1036 | core+data |
| 4 | `payment_service` | `tst_payment_service.cpp` | 495 | core+data |
| 5 | `return_service` | `tst_return_service.cpp` | 409 | core+data |
| 6 | `supplier_report` | `tst_supplier_report.cpp` | 347 | core+data |
| 7 | `daily_report` | `tst_daily_report.cpp` | 382 | core+data |
| 8 | `device` | `tst_device.cpp` | 267 | core+data |
| 9 | `coordinator` | `tst_coordinator.cpp` | 253 | core+data+network |
| 10 | `finance` | `tst_finance.cpp` | 75 | core |
| 11 | `sync` | `tst_sync.cpp` | 123 | core+network |
| 12 | `sync_server` | `tst_sync_server.cpp` | 568 | core+data+network |
| 13 | `scheduler` | `tst_scheduler.cpp` | 242 | core+data+network |
| 14 | `ui` | `tst_ui.cpp` | 1792 | ui |
| 15 | `sync_client` | `tst_sync_client.cpp` | 271 | core+data+network |
| 16 | `update` | `tst_update.cpp` | 226 | core |

### How to run
```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure     # whole suite
ctest --test-dir build -R "^data$" -V          # one suite, verbose, per-test PASS lines
QT_QPA_PLATFORM=offscreen ./build/tests/tst_ui # UI suite directly, headless
```

### Current status — measured, not assumed
Re-run while writing this dump:
```
$ ctest --test-dir build
...
15/16 Test #15: sync_client ......................   Passed    1.71 sec
16/16 Test #16: update ...........................   Passed    0.03 sec

100% tests passed, 0 tests failed out of 16

Total Test time (real) =  37.46 sec
```
Per-suite totals:
- `data`: `Totals: 98 passed, 0 failed` (8595 ms)
- `ui` (offscreen): `Totals: 33 passed, 0 failed` (6860 ms) — note the README calls this
  "19 سيناريو", so even the per-suite count in the docs is stale

One benign message appears after the `data` suite finishes:
```
QSqlDatabasePrivate::removeDatabase: connection 'mahali_<uuid>' is still in use,
all queries will cease to work.
```
That is the legacy-DB migration tests holding a raw `QSqlDatabase` on a fixed connection
name; it fires at teardown after the last assertion and does not fail anything.

**Tests are NOT executed in CI** (the workflow builds `--target mahali-desktop` only).

### Notable test conventions
- Legacy-DB migration tests open a raw `QSqlDatabase` with a fixed connection name,
  hand-create pre-feature tables, set `PRAGMA user_version = N-1`, then call
  `data::runSchemaMigrations()` and assert both that the new shape arrived **and that
  running it twice is a no-op**. See `adjustment_migration_adds_the_column_to_a_legacy_ledger()`
  (declared `tests/tst_data.cpp:93`, defined at `:1072`) for the current template.
  `tests/tst_data.cpp:1339` also asserts the 5 reversal indexes exist by name, which is
  the test that would catch the fresh-install index question in Appendix A.
- `CashSessionRepository::open()` **refuses a non-positive opening float**
  (`data/cash_session_repository.cpp:89`, `"cash session refused: non-positive opening
  float"`). So `open(0)` throws/returns false and any test that needs a session must pass
  a real float.
- UI tests assert exact Arabic notice strings: `pos_page.cpp:1008` emits
  `tr("تم البيع: %1").arg(formatMoney(result.totalCents))` and `tests/tst_ui.cpp:396`
  asserts `noticeText().contains("تم البيع")`.

---

## 9. TODOs and known issues

### 9.1 Marker comments
```
$ grep -rnE "\b(TODO|FIXME|HACK|XXX|BUG)\b" --include=*.cpp --include=*.h .
0 matches
```
**The codebase has zero TODO/FIXME/HACK/XXX/BUG markers.** Intent is instead carried by
prose comments that explain *why*, often including the failure mode being prevented.

### 9.2 Issues visible in the tree (not marker-based)

1. **`apps/android/main.cpp` is missing.** `apps/android/CMakeLists.txt:11` does
   `add_executable(mahali-android main.cpp)`. Configures only when `-DMAHALI_ANDROID=ON`,
   so it is invisible today, but that option is broken.
2. **`README.md` says "10 اختبارات"** while there are 16 ctest targets.
3. **Tracked build artefacts:** `build.log`, `Testing/Temporary/*`. Not in `.gitignore`.
4. **Hardcoded default sync HMAC key** (`apps/desktop/main.cpp:72`, value redacted).
   Shops that never open Settings share one signing key.
5. **`verifyMaster` compares hashes with `==`** — not constant-time.
6. **`translations/mahali_en.ts` has 292 `unfinished` entries.** fr and ar are at
   **0 unfinished (596/596)**; English is essentially untranslated (Arabic source
   strings fall through). It is nevertheless compiled and embedded by the build.
7. **`lupdate` scope trap.** `-no-obsolete` combined with a partial source list drops
   live entries: any source directory left out of the `lupdate` invocation has its strings
   removed from the catalogue, not just left stale. The correct scope is
   `ui data core apps` — **`apps` must be included**, because `apps/desktop/main.cpp`
   owns the `tr()` calls for `"محلي"`, `"محلي — خطأ"` and
   `"تعذر فتح قاعدة البيانات:\n%1"` (all three are present in `mahali_fr.ts` today, so they
   are currently safe). Verify with `git diff translations/` after any `lupdate` run.
8. **`.vscode/launch.json` launches Chrome against `http://localhost:8080`** — a non-Qt
   web app, with the stock VS Code "learn about possible attributes" boilerplate comments.
   Unrelated to this project and actively misleading in an editor.
9. **`lrelease` invoked bare writes its `.qm` next to the `.ts`**, littering
   `translations/` with untracked artefacts that `.gitignore` does not cover. Harmless for
   the build — `apps/desktop/CMakeLists.txt` uses `lrelease <abs.ts> -qm <build-tree path>`
   and embeds the results via a generated `mahali_translations.qrc` — but it means a
   manual `lrelease translations/mahali_fr.ts` is not the same operation the build does.
10. **`build.log` records exactly two warnings**, both in test/demo code, and warnings are
    not treated as errors anywhere:
    - `ui/login_dialog.cpp:245` — `unused parameter 'userId'` in
      `LoginDialog::showPinInput(int userId, const QString& userName)`
    - `tests/tst_ui.cpp:435` — `unused variable 'productId'` in
      `posSaleRequiresOpenSession()`
11. **Commit `13cba43` is titled "WIP: audit fixes (unverified)"** — audit-log work
    landed self-described as unverified. Worth a look before trusting audit output.
12. **`SyncProtocol::errorClassForStatus`** returns `Network` for 3xx/1xx, which is then
    treated as retryable; a redirect could loop.
13. **User PINs are stored in plaintext** (`users.pin`), see 6.2. This is the most
    consequential finding in this dump: anyone who can read the SQLite file learns every
    operator's PIN. The admin master password in the same schema *is* hashed with
    PBKDF2, so the codebase clearly knows how — this looks like an oversight rather than
    a decision. Worth fixing before the shop's data ever leaves the machine.

### 9.3 Known scope boundaries (documented in-tree, not bugs)
- Android POS is a **later phase**; only the data/service layers are shared so far
  (README, and `core/CMakeLists.txt` comment on UpdateChecker's Network dep).
- `core::SyncOpType` is only `Sale`, `CustomerDebt`, `CustomerPayment`, and **reversals
  never sync**. This is deliberate and documented in the comment above
  `struct PartialUniqueIndex` (`data/schema_migrations.cpp:126-131`): all three op types
  arrive as ordinary rows with the sentinel `0`, so "the server applies a sale, a debt
  or a repayment, never a reversal… it cannot receive it" — which is why the server
  needs no branch to recognise one, and why no inbound batch can trip those indexes.
- Supplier returns do not recompute PMP ("point for later discussion",
  `docs/FINANCIAL_MODEL.md:151`).
- `reports_page` has a `makeStubPage()` helper for roadmap pages not yet written.

---

## 10. Recent changes

### 10.1 Last commit `6d32a5d` — "feat(products): delete unused, disable sold; i18n updates"
```
- Delete permanent only when no sale/purchase history
- Otherwise disable (uncheck Actif)
- Ajustement: adjustment_cents on sales + customer_transactions
- POS: adjustment field in invoice bar, ghost row in cart
- Translation: 596/596 in fr + ar

 core/customer_transaction.h              |   5 +
 core/sale.h                              |   7 +
 data/customer_transaction_repository.cpp |  12 +-
 data/database.cpp                        |   6 +
 data/sale_repository.cpp                 |  12 +-
 data/sale_service.cpp                    |  36 +++++-
 data/sale_service.h                      |  15 ++-
 data/schema_migrations.cpp               |  56 +++++++++++++++
 data/schema_migrations.h                 |   2 +-
 tests/tst_data.cpp                       | 212 +++++++++++++++++++++++++++++++
 translations/mahali_ar.ts                |  95 +++++++++++---
 translations/mahali_fr.ts                |  99 +++++++++++----
 ui/pos_page.cpp                          | 148 ++++++++++++++++++++++++-
 ui/pos_page.h                            |  19 +
 14 files changed, 660 insertions(+), 64 deletions(-)
```

**The Ajustement (invoice adjustment) feature**, shipped end-to-end:
- `adjustment_cents INTEGER NOT NULL DEFAULT 0` on both `sales` and `customer_transactions`
- In `createSchema()` (fresh installs) **and** as numbered migration 12 with a
  `PRAGMA table_info` guard (existing installs); `kSchemaVersion` 11 → 12
- Column added **last** in `kSaleColumns` / `kTxColumns` so the positional indices of
  the pre-existing columns keep their meaning
- `recordSale` / `recordCustomerDebt` gained a trailing
  `long long adjustmentCents = 0` — every pre-existing caller unchanged and still
  recording `0`
- `total = Σlines + adjustment` (overflow-guarded); **COGS deliberately unchanged**
- Reversals mirror the adjustment with a flipped sign
- POS: `QLineEdit` (placeholder `+ 0`, objectName `posAdjustmentField`) under the hero
  Total in `invoiceBar`; sign handled manually because `parseMoney` rejects negatives;
  unparsable text **refuses the sale** rather than silently recording 0
- POS: a **ghost row** in the cart grid when the adjustment ≠ 0 — no `productId`, no
  barcode, no quantity, index past `m_lines`, so `onRemoveLine` skips it and
  `syncFromTable` accepts `rowCount == size + 1`
- 4 new tests in `tst_data.cpp`: `sale_with_adjustment`,
  `customer_debt_with_adjustment`, `sale_without_adjustment_unchanged`,
  `adjustment_migration_adds_the_column_to_a_legacy_ledger`
- fr + ar catalogues went 586 → **596 messages, 0 unfinished**. All 10 new strings were
  added *and* translated in this one commit — verified by diffing the catalogues against
  `3c2c911`, where both sat at 586 messages / 0 unfinished. So the Ajustement feature
  shipped with its translations complete rather than leaving gaps for a later `lupdate`.

**The product half of the same commit** (`canDeletePermanently()` / `removePermanently()`,
`data/product_repository.h:59-67`) is the best example of the codebase's style, and worth
reading before writing any new destructive operation:
- hard delete is allowed **only** when no sale or purchase document references the product;
  otherwise the row is kept and merely deactivated
- **stock movements are deliberately excluded** from the reference count — "a movement is
  a trail of the product's own count rather than a document somebody else reads, so it goes
  with the row"
- a table that cannot be read answers "not deletable", never "no references" — the comment
  spells out why: "answering yes would hand the caller a delete that then fails, and
  keeping the product is the answer that loses nothing"
- `removePermanently()` **re-reads the reference count inside the transaction** instead of
  trusting the caller's earlier `canDeletePermanently()`, "so a document written in between
  cannot be orphaned by a row that then disappears"; on any failure it leaves nothing behind

### 10.2 Commits 3c2c911 and earlier
- `3c2c911` — the original product delete/disable work; `6d32a5d` is its i18n + Ajustement follow-up
- `e4b1e0e` "i18n: complete French and Arabic translation (586/586)"
- `87df98a` zakat overhaul + the credit-sale button in POS
- `826c413` zakat notifier fix + history table + reports log
- `4f779ae` "Fix cash reference test, remove stock trigger; 16/16 green"
- `378ba01` / `5c19f90` UI polish passes (the `ui-polish-shots/` before/after corpus)
- `13cba43` "WIP: audit fixes (unverified)"

### 10.3 Uncommitted work
**None.** Tree is clean and identical to `origin/main`.

---

## 11. Existing AI context

```
$ ls AGENTS.md CONTEXT.md TASKS.md DECISIONS.md COMMANDS.md CLAUDE.md .cursorrules .opencode
ls: cannot access 'AGENTS.md': No such file or directory
... (none exist)
```

**There is no AI-assistant context file of any kind in this repository** — no
`AGENTS.md`, no `.opencode/`, no `CLAUDE.md`, no `.cursorrules`, no decision log, no
task file. This `CONTEXT_DUMP.md` is the first.

The closest thing to institutional memory is:
- `docs/FINANCIAL_MODEL.md` — the accounting rules of record
- the prose comments throughout, which explain *why* rather than *what* and are
  consistently written in English while all UI strings are Arabic
- `README.md` — build and distribution instructions (Arabic)

**Style conventions an assistant should match** (inferred, high confidence):
- Comments are long, narrative, and argue the failure mode they prevent. A one-line
  comment is a smell here.
- Money is always `long long` cents; `optional<long long>` for "too large / too big to
  store"; overflow is *checked*, never performed.
- Ledger tables are append-only: a reversal is a mirrored negative row with a
  `reversed_*_id`, never an UPDATE or DELETE. Deletion is rare; prefer `active = 0`.
- Every write goes through a service inside a transaction, and the service (not the
  caller) writes the audit-log row.
- UI: Arabic labels via `tr()`, `setObjectName()` for QSS targeting, `theme_tokens.h`
  (spacing/colour constants) and `scan_safe_dialog.h` (a scan-safe `QDialog` base), icons
  drawn in code rather than shipped as pixmaps.
- Tests assert the *figure that was wrong before*, with a comment naming it.

---

## 12. Missing information / questions to ask the user

**Could not determine from the repo:**
1. **Is `adjustment_cents` supposed to sync?** `SyncOperation`/`SyncOpType` carries
   `amountCents` but has no adjustment field, and `SyncProcessor::applySale` calls
   `recordSale` without the new argument — so **a device sale with an adjustment syncs
   its total but loses the adjustment column on the server.** Is that intended, or is
   protocol work pending?
2. **Should `adjustment_cents` appear in reports / the receipts / the sales page?**
   Nothing outside `pos_page` and `sale_service` reads it yet. `docs/FINANCIAL_MODEL.md`
   has not been updated for it either.
3. **Printed invoices.** The Ajustement spec mentioned "الفاتورة المطبوعة", but no
   printing/PDF code exists in the tree. Where does a printed invoice come from?
4. **Negative totals.** A discount larger than the cart produces a negative `total_cents`
   and therefore a negative cash movement. Intended, or should it be refused?
5. **`mahali_en.ts`** — is English meant to be a real locale, or is ar+fr the shipping
   pair with `en.ts` a placeholder?
6. **Android phase status** — is `apps/android/main.cpp` still owed?
7. **Deployment / release channel.** The self-update already targets a concrete repo, so
   the slug is settled: `core/update_checker.cpp:19` polls
   `https://api.github.com/repos/heaxheax15-pixel/mahali/releases/latest` and
   `core/update_downloader.cpp:19` fetches
   `https://github.com/heaxheax15-pixel/mahali/releases/latest/download/Mahali-Setup.zip`
   — i.e. the update path expects a **published GitHub Release whose asset is named
   `Mahali-Setup.zip`**, which is exactly what the CI `Zip` step produces. What is not in
   the repo is the missing link: nothing publishes a Release or tag. Is that a manual
   step, or is CI supposed to do it?
8. **Localization direction.** `applyLanguage()` pins `Qt::RightToLeft` for `ar` and
   `Qt::LeftToRight` otherwise (`core/i18n.cpp:87-88`). Is a French/English LTR POS layout
   actually validated, or is ar the only real target?

**Suggested first questions:** (1) and (3) are the ones that most affect correctness.

---

## Appendix A — quick reference: schema invariants

- All money = `long long` integer cents. No floats, ever.
- All timestamps = ISO-8601 UTC text.
- Every `id` = `INTEGER PRIMARY KEY AUTOINCREMENT`.
- `total_cents == Σ(sale_items) + adjustment_cents` (holds for reversals too).
- `cogs` is always derived from `unit_cost_cents` frozen at sale time, never recomputed.
- Customer balance (`CustomerRepository::balanceCentsFor()`,
  `data/customer_repository.cpp:174-210`) is **three** sources, not one:
  ```
  opening_balance_cents (customers)
  + COALESCE(SUM(customer_transactions.amount_cents))   -- no sign filter
  − COALESCE(SUM(payments.amount_cents))                 -- no sign filter
  ```
  Neither sum filters on sign, and the comment says exactly why: a reversal is a negative
  row marked by `reversed_transaction_id`, so `WHERE amount_cents > 0` would drop every
  reversal and leave a cancelled credit sale still showing as debt; a refunded payment is
  a negative row for the same reason.
- `createSchema()` does **not** create `customers.opening_balance_cents` (nor
  `suppliers.{address,notes,opening_balance_cents}`) — those columns come from
  `migrateCustomersTable()` (`database.cpp:130`, adds the column at `:138`) and
  `migrateSuppliersTable()` (`:96`). Neither is in the versioned `kMigrations` table: the
  `Database` constructor calls a whole series of **unversioned** helpers
  (`database.cpp:798-805`) — `migrateZakatSettingsIntoSettings`, `migrateUsersTable`,
  `migrateCustomersTable`, `migrateSuppliersTable`, `migrateStockMovementsTable`,
  `migrateSalesTable`, `migrateProductsTable` — each of which `ALTER`s whatever it finds
  missing. Only `migrateProductsTable` reports an error; the rest fail silently.
- `createSingleOpenSessionIndex()` is kept out of the schema statement list **on purpose**:
  the schema list throws on the first failing statement, whereas a database copied from an
  older build can already hold two open cash sessions, making the index impossible to
  create. Such a database must keep working, so the failure is reported and the app runs
  on without the index — meaning "one open session at a time" is then enforced by
  `CashSessionRepository::open()` alone.
- Cash sessions: at most one open, enforced by `idx_cash_sessions_one_open`.
- One reversal per original, enforced **twice**:
  1. by 5 partial unique indexes created by migration 4 —
     `uq_payments_one_reversal`, `uq_expenses_one_reversal`,
     `uq_owner_drawings_one_reversal`, `uq_sales_one_reversal`,
     `uq_customer_transactions_one_reversal`, each `UNIQUE(col) WHERE col <> 0`
     (migration 4 skips an index, with a loud `qWarning`, if the table already holds
     duplicate reversals);
  2. by a service-level `SELECT 1 ... WHERE reversed_col = ?` performed **inside** the
     write transaction, so the rule still holds on a database that never got the index.

  Corollary worth knowing: `createSchema()` does **not** create these 5 indexes (nor the
  15 report indexes). They are only reachable through the migration path — which a fresh
  install does run, because `PRAGMA user_version` starts at 0.

## Appendix B — commands run for this dump (all read-only)

```bash
find . -maxdepth 4 -type f -not -path "./.git/*" -not -path "./build/*" ... | sort
git status; git branch --show-current; git log --oneline -20
git show --stat 6d32a5d; git reflog -5; git ls-files | grep -E "^(Testing|build\.log)"
cat CMakeLists.txt core/CMakeLists.txt data/CMakeLists.txt network/CMakeLists.txt \
    ui/CMakeLists.txt tests/CMakeLists.txt apps/*/CMakeLists.txt
cat README.md .github/workflows/build-windows.yml .vscode/launch.json .gitignore \
    docs/FINANCIAL_MODEL.md core/session.h core/version.h core/i18n.h core/sync_operation.h \
    ui/theme.h ui/server_controller.h data/database.h data/report_service.h \
    data/device_ledger_service.h network/sync_processor.h
head -50 network/sync_server.cpp; sed -n '925,975p' ui/main_window.cpp
grep -rnE "\b(TODO|FIXME|HACK|XXX|BUG)\b" --include=*.cpp --include=*.h .      # → 0
grep -rniE "(password|secret|token|api[_-]?key|hmac|credential)" core/ data/ network/ apps/
wc -l <all cpp/h>; ctest --test-dir build -N; ctest --test-dir build --output-on-failure
```