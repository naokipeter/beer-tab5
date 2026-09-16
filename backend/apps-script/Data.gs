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
