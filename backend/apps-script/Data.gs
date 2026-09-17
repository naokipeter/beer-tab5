/**
 * Sheet reads and writes. Everything goes through here so the column order lives
 * in exactly one place (Config.gs) and a reordered sheet cannot silently write a
 * price into the name column.
 */

function rowsOf_(sheet, columns) {
  var last = sheet.getLastRow();
  if (last < 2) return [];
  var values = sheet.getRange(2, 1, last - 1, columns.length).getValues();
  return values.map(function(row) {
    var obj = {};
    for (var i = 0; i < columns.length; i++) obj[columns[i]] = row[i];
    obj._row = 0;  // filled by the caller when it matters
    return obj;
  });
}

function readProducts_() {
  var sh = productsSheet_();
  var last = sh.getLastRow();
  if (last < 2) return [];
  var values = sh.getRange(2, 1, last - 1, PRODUCT_COLUMNS.length).getValues();
  var out = [];
  for (var i = 0; i < values.length; i++) {
    var r = values[i];
    var name = cleanText_(r[1], LIMITS.maxNameChars);
    if (!name) continue;  // a nameless row is a stray edit, not a product
    out.push({
      barcode: cleanText_(r[0], LIMITS.maxBarcodeChars),
      name: name,
      price_rappen: cleanPrice_(r[2]) === null ? 0 : cleanPrice_(r[2]),
      free: r[3] === true || String(r[3]).toLowerCase() === 'true',
      image_url: cleanImageUrl_(r[4]),
      // Anything other than an explicit FALSE counts as on the shelf, so a blank
      // cell in a hand-edited sheet does not empty the fridge.
      active: !(r[5] === false || String(r[5]).toLowerCase() === 'false')
    });
  }
  return out;
}

function readResidents_() {
  var sh = residentsSheet_();
  var last = sh.getLastRow();
  if (last < 2) return [];
  var values = sh.getRange(2, 1, last - 1, RESIDENT_COLUMNS.length).getValues();
  var out = [];
  for (var i = 0; i < values.length; i++) {
    var id = cleanText_(values[i][0], 11);
    var name = cleanText_(values[i][1], 23);
    var active = !(values[i][2] === false || String(values[i][2]).toLowerCase() === 'false');
    if (!id || !name || !active) continue;
    out.push({id: id, name: name});
  }
  return out;
}

function findResident_(residentId) {
  if (!residentId) return null;
  var sh = residentsSheet_();
  var last = sh.getLastRow();
  if (last < 2) return null;
  var values = sh.getRange(2, 1, last - 1, RESIDENT_COLUMNS.length).getValues();
  for (var i = 0; i < values.length; i++) {
    if (cleanText_(values[i][0], 11) !== residentId) continue;
    var active = !(values[i][2] === false || String(values[i][2]).toLowerCase() === 'false');
    if (!active) return null;
    return {resident_id: residentId, name: cleanText_(values[i][1], 23)};
  }
  return null;
}

/**
 * Drinks and totals per resident, excluding voided purchases. Used by both the
 * device sync and the management page, so the two can never disagree.
 */
function consumptionSummary_() {
  var sh = purchasesSheet_();
  var last = sh.getLastRow();
  var out = [];
  if (last < 2) return out;

  var values = sh.getRange(2, 1, last - 1, PURCHASE_COLUMNS.length).getValues();
  var iPrice = PURCHASE_COLUMNS.indexOf('price_rappen');
  var iId = PURCHASE_COLUMNS.indexOf('resident_id');
  var iName = PURCHASE_COLUMNS.indexOf('resident_name');
  var iVoid = PURCHASE_COLUMNS.indexOf('voided_at');
  var byId = {};

  for (var i = 0; i < values.length; i++) {
    if (values[i][iVoid]) continue;
    var id = cleanText_(values[i][iId], 11) || '?';
    if (!byId[id]) {
      byId[id] = {
        resident_id: id,
        name: cleanText_(values[i][iName], 23) || id,
        drinks: 0,
        total_rappen: 0
      };
      out.push(byId[id]);
    }
    byId[id].drinks++;
    byId[id].total_rappen += Number(values[i][iPrice]) || 0;
  }
  // The terminal can only show so many rows.
  return out.slice(0, LIMITS.maxResidents);
}

/**
 * One resident's consumption, broken down by product. Fetched on demand rather
 * than ridden along on every sync: the full residents-by-products matrix would
 * be larger than the terminal's response buffer in the worst case, while one
 * person's breakdown is a handful of rows.
 */
function residentBreakdown_(residentId) {
  var sh = purchasesSheet_();
  var last = sh.getLastRow();
  var out = [];
  if (last < 2) return out;

  var values = sh.getRange(2, 1, last - 1, PURCHASE_COLUMNS.length).getValues();
  var iName = PURCHASE_COLUMNS.indexOf('product_name');
  var iPrice = PURCHASE_COLUMNS.indexOf('price_rappen');
  var iResident = PURCHASE_COLUMNS.indexOf('resident_id');
  var iVoid = PURCHASE_COLUMNS.indexOf('voided_at');
  var byProduct = {};

  for (var i = 0; i < values.length; i++) {
    if (values[i][iVoid]) continue;
    if (cleanText_(values[i][iResident], 11) !== residentId) continue;
    // Grouped by the name recorded at purchase time, so renaming a product later
    // does not silently merge or split someone's history.
    var name = cleanText_(values[i][iName], LIMITS.maxNameChars) || '?';
    if (!byProduct[name]) {
      byProduct[name] = {name: name, drinks: 0, total_rappen: 0};
      out.push(byProduct[name]);
    }
    byProduct[name].drinks++;
    byProduct[name].total_rappen += Number(values[i][iPrice]) || 0;
  }

  out.sort(function(a, b) { return b.drinks - a.drinks; });
  // The terminal shows one screenful; the rest would not be readable anyway.
  return out.slice(0, 12);
}

/** 1-based sheet row of a transaction id, or 0. */
function findPurchaseRow_(transactionId) {
  var sh = purchasesSheet_();
  var last = sh.getLastRow();
  if (last < 2) return 0;
  var ids = sh.getRange(2, 1, last - 1, 1).getValues();
  for (var i = 0; i < ids.length; i++) {
    if (String(ids[i][0]) === transactionId) return i + 2;
  }
  return 0;
}

/** 1-based sheet row of a product, matched by barcode when present, else name. */
function findProductRow_(barcode, name) {
  var sh = productsSheet_();
  var last = sh.getLastRow();
  if (last < 2) return 0;
  var values = sh.getRange(2, 1, last - 1, 2).getValues();
  for (var i = 0; i < values.length; i++) {
    var rowBarcode = cleanText_(values[i][0], LIMITS.maxBarcodeChars);
    if (barcode && rowBarcode === barcode) return i + 2;
    if (!barcode && !rowBarcode && cleanText_(values[i][1], LIMITS.maxNameChars) === name) {
      return i + 2;
    }
  }
  return 0;
}

/**
 * Shelves or archives a product. Shared by the management page and the terminal
 * so the two cannot enforce different rules. Caller holds the lock.
 */
function setProductActive_(barcode, name, active) {
  var b = cleanText_(barcode, LIMITS.maxBarcodeChars);
  var n = cleanText_(name, LIMITS.maxNameChars);
  if (active === true) assertRoomForActive_(b, n);
  var row = findProductRow_(b, n);
  if (row <= 0) throw new Error('Produkt nicht gefunden');
  var sh = productsSheet_();
  sh.getRange(row, PRODUCT_COLUMNS.indexOf('active') + 1).setValue(active === true);
  sh.getRange(row, PRODUCT_COLUMNS.indexOf('updated_at') + 1).setValue(new Date());
  bumpRevision_();
}

/**
 * Changes a price. Past purchases keep the price recorded at the time, so a
 * promotion never rewrites what anyone already owes. Caller holds the lock.
 */
function setProductPrice_(barcode, name, priceRappen, free) {
  var price = free === true ? 0 : cleanPrice_(priceRappen);
  if (price === null) throw new Error('Preis ungueltig');
  if (free !== true && price === 0) throw new Error('Preis fehlt');
  var row = findProductRow_(cleanText_(barcode, LIMITS.maxBarcodeChars),
                            cleanText_(name, LIMITS.maxNameChars));
  if (row <= 0) throw new Error('Produkt nicht gefunden');
  var sh = productsSheet_();
  sh.getRange(row, PRODUCT_COLUMNS.indexOf('price_rappen') + 1).setValue(price);
  sh.getRange(row, PRODUCT_COLUMNS.indexOf('free') + 1).setValue(free === true);
  sh.getRange(row, PRODUCT_COLUMNS.indexOf('updated_at') + 1).setValue(new Date());
  bumpRevision_();
}

function writeProductRow_(row, product) {
  productsSheet_()
      .getRange(row, 1, 1, PRODUCT_COLUMNS.length)
      .setValues([[
        product.barcode, product.name, product.price_rappen, product.free,
        product.image_url, product.active, new Date()
      ]]);
}

function appendProduct_(product) {
  productsSheet_().appendRow([
    product.barcode, product.name, product.price_rappen, product.free,
    product.image_url, product.active, new Date()
  ]);
}
