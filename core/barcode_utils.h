#pragma once

#include <QString>

namespace app::core {

// A barcode scanner is a keyboard: it types its digits through whichever
// keyboard layout the OS is running. On a French AZERTY machine the unshifted
// number row is not 1 2 3 4 5 6 7 8 9 0, it is & é " ' ( - è _ ç à, so a scan of
// 610300661059 lands in the field as  -&"à"""-àà(-àç  and no product is found.
//
// normalizeScannedBarcode turns that row back into the digits it stands for.
//
// It is deliberately conservative, because every field that takes a scan also
// takes a product name, and the letters this map touches are everyday French:
// "Crème", "Café" and "Pâté" must survive untouched, not become "Cr7me",
// "Caf2" and "Pat3". So the text is only rewritten when *every* character
// belongs to the scanner alphabet (ASCII digits plus the ten symbols of the
// AZERTY row). A single letter, or any other character, means a human is
// typing and the string is returned as it came in.
QString normalizeScannedBarcode(const QString& input);

} // namespace app::core
