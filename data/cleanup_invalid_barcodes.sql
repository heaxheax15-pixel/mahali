-- One-off data repair for products.barcode. No schema change: the column is
-- already `barcode TEXT UNIQUE` and NULL is how this codebase spells "no
-- barcode" (ProductRepository::barcodeVariant binds a blank barcode as SQL
-- NULL, and migrateProductsTable() already normalised blank ones to NULL).
--
-- Run it against a stopped app, on a copy first if the database is live:
--   sqlite3 path/to/mahali.sqlite < data/cleanup_invalid_barcodes.sql
-- The script is idempotent: a second run reports zero changes.
--
-- Symptom it repairs: a scanner test left a barcode that is not a barcode at
-- all, and the Produits grid prints the stored string verbatim, so the row read
-- as `}&ààà-ààà"&` under the code-barres column.

BEGIN;

-- 1. The row the corrupt barcode belonged to is removed first, and this is not
--    cosmetic: normalising the barcode to NULL without deleting the row would
--    turn it into a POS quick item, because ProductRepository::kQuickItemsFilter
--    selects `barcode IS NULL OR TRIM(barcode) = ''`. That trades a garbled
--    barcode for a nameless tile in Vente rapide and a blank cart line.
--    It has to run before the UPDATE below: that statement clears the barcode,
--    and a NULL then fails this one's `barcode IS NOT NULL` guard.
--    The predicate is deliberately narrow, so it can only catch the junk row
--    and never a real product:
--      - the barcode is not a usable code, so a nameless product an operator
--        created deliberately (ProductDialog does not require a name) is left
--        alone;
--      - and nothing anywhere points at it, so no sale, purchase, movement or
--        return is orphaned by the delete.
DELETE FROM products
 WHERE barcode IS NOT NULL
   AND (length(barcode) NOT BETWEEN 8 AND 14
        OR barcode GLOB '*[^0-9]*')
   AND TRIM(COALESCE(name, '')) = ''
   AND quantity = 0
   AND cost_price_cents = 0
   AND NOT EXISTS (SELECT 1 FROM sale_items WHERE product_id = products.id)
   AND NOT EXISTS (SELECT 1 FROM purchase_items WHERE product_id = products.id)
   AND NOT EXISTS (SELECT 1 FROM stock_movements WHERE product_id = products.id)
   AND NOT EXISTS (SELECT 1 FROM customer_transaction_items WHERE product_id = products.id)
   AND NOT EXISTS (SELECT 1 FROM supplier_return_items WHERE product_id = products.id);

-- 2. Every barcode still stored that is not 8 to 14 digits becomes SQL NULL.
--    Both halves of the test are needed: length() counts characters, so a
--    13-character value of accented letters passes the length check and is only
--    caught by the GLOB, while a 5-digit value passes the GLOB and is only
--    caught by the length check. GLOB is case sensitive and takes a range
--    negated by ^, so '*[^0-9]*' means "contains a character that is not a
--    digit" -- SQLite's stand-in for the regex it does not have.
UPDATE products
   SET barcode = NULL
 WHERE barcode IS NOT NULL
   AND (length(barcode) NOT BETWEEN 8 AND 14
        OR barcode GLOB '*[^0-9]*');

COMMIT;

-- Report. The count is the check: zero means every remaining barcode is 8 to 14
-- digits, and the listing is what the Produits grid will now print.
SELECT count(*) AS unusable_barcodes_left
  FROM products
 WHERE barcode IS NOT NULL
   AND (length(barcode) NOT BETWEEN 8 AND 14
        OR barcode GLOB '*[^0-9]*');

SELECT id,
       quote(barcode) AS barcode,
       name
  FROM products
 ORDER BY id;
