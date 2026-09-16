/**
 * The management page and the functions it calls.
 *
 * These run under the caller's own Google account through google.script.run, so
 * there is a real identity to check against ADMIN_EMAILS. That is why the
 * management deployment must be set to "Anyone with a Google account": on the
 * anonymous device deployment Session.getActiveUser() is empty and every call
 * below refuses.
 */

function doGet() {
  // Guard the page itself, so the anonymous device deployment serves nothing.
  try {
    requireAdmin_();
  } catch (err) {
    return HtmlService.createHtmlOutput(
        '<p style="font-family:sans-serif">Kein Zugriff.</p>');
  }
  return HtmlService.createHtmlOutputFromFile('manage')
      .setTitle('Bierkasse verwalten')
      .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

/** Everything the page needs on load. */
function adminLoad() {
  requireAdmin_();
  return {
    products: readProducts_(),
    residents: readResidents_(),
    revision: revision_(),
    activeCount: readProducts_().filter(function(p) { return p.active; }).length,
    maxActive: LIMITS.maxActiveProducts
  };
}

/**
 * Creates a product or updates the existing one with the same barcode. Returns
 * the fresh catalog so the page never has to guess what the server now holds.
 */
function adminSaveProduct(input) {
  requireAdmin_();

  var barcode = cleanText_(input.barcode, LIMITS.maxBarcodeChars);
  if (!isValidBarcode_(barcode)) throw new Error('Barcode-Pruefziffer stimmt nicht');

  var name = cleanText_(input.name, LIMITS.maxNameChars);
  if (!name) throw new Error('Name fehlt');

  var free = input.free === true;
  var price = free ? 0 : cleanPrice_(input.price_rappen);
  if (price === null) throw new Error('Preis ungueltig');
  if (!free && price === 0) throw new Error('Preis fehlt');

  var product = {
    barcode: barcode,
    name: name,
    price_rappen: price,
    free: free,
    image_url: cleanImageUrl_(input.image_url),
    active: input.active !== false
  };

  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    if (product.active) assertRoomForActive_(barcode, name);
    var row = findProductRow_(barcode, name);
    if (row > 0) {
      writeProductRow_(row, product);
    } else {
      appendProduct_(product);
    }
    bumpRevision_();
  } finally {
    lock.releaseLock();
  }
  return adminLoad();
}

/** Price changes are separate from creation so the page can confirm them. */
function adminChangePrice(barcode, name, priceRappen, free) {
  requireAdmin_();
  var price = free === true ? 0 : cleanPrice_(priceRappen);
  if (price === null) throw new Error('Preis ungueltig');
  if (free !== true && price === 0) throw new Error('Preis fehlt');

  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    var row = findProductRow_(cleanText_(barcode, LIMITS.maxBarcodeChars),
                              cleanText_(name, LIMITS.maxNameChars));
    if (row <= 0) throw new Error('Produkt nicht gefunden');
    var sh = productsSheet_();
    sh.getRange(row, PRODUCT_COLUMNS.indexOf('price_rappen') + 1).setValue(price);
    sh.getRange(row, PRODUCT_COLUMNS.indexOf('free') + 1).setValue(free === true);
    sh.getRange(row, PRODUCT_COLUMNS.indexOf('updated_at') + 1).setValue(new Date());
    bumpRevision_();
  } finally {
    lock.releaseLock();
  }
  return adminLoad();
}

function adminSetActive(barcode, name, active) {
  requireAdmin_();
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    var b = cleanText_(barcode, LIMITS.maxBarcodeChars);
    var n = cleanText_(name, LIMITS.maxNameChars);
    if (active === true) assertRoomForActive_(b, n);
    var row = findProductRow_(b, n);
    if (row <= 0) throw new Error('Produkt nicht gefunden');
    var sh = productsSheet_();
    sh.getRange(row, PRODUCT_COLUMNS.indexOf('active') + 1).setValue(active === true);
    sh.getRange(row, PRODUCT_COLUMNS.indexOf('updated_at') + 1).setValue(new Date());
    bumpRevision_();
  } finally {
    lock.releaseLock();
  }
  return adminLoad();
}

/**
 * The terminal's grid holds a fixed number of tiles, so the shelf is capped here
 * rather than silently trimmed at sync time.
 */
function assertRoomForActive_(barcode, name) {
  var products = readProducts_();
  var active = 0;
  var alreadyActive = false;
  for (var i = 0; i < products.length; i++) {
    var p = products[i];
    var same = (barcode && p.barcode === barcode) || (!barcode && p.name === name);
    if (p.active) {
      active++;
      if (same) alreadyActive = true;
    }
  }
  if (!alreadyActive && active >= LIMITS.maxActiveProducts) {
    throw new Error('Kuehlschrank voll: zuerst ein Bier archivieren');
  }
}

function adminSaveResident(residentId, name, active) {
  requireAdmin_();
  var id = cleanText_(residentId, 11);
  var n = cleanText_(name, 23);
  if (!id || !n) throw new Error('ID und Name noetig');

  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    var sh = residentsSheet_();
    var last = sh.getLastRow();
    var row = 0;
    if (last >= 2) {
      var ids = sh.getRange(2, 1, last - 1, 1).getValues();
      for (var i = 0; i < ids.length; i++) {
        if (cleanText_(ids[i][0], 11) === id) { row = i + 2; break; }
      }
    }
    if (row > 0) {
      sh.getRange(row, 1, 1, RESIDENT_COLUMNS.length)
          .setValues([[id, n, active !== false]]);
    } else {
      if (readResidents_().length >= LIMITS.maxResidents) {
        throw new Error('Zu viele Personen fuer das Terminal');
      }
      sh.appendRow([id, n, active !== false]);
    }
    bumpRevision_();
  } finally {
    lock.releaseLock();
  }
  return adminLoad();
}

/** Consumption summary, for the page. Voided purchases are excluded. */
function adminSummary() {
  requireAdmin_();
  var sh = purchasesSheet_();
  var last = sh.getLastRow();
  var byResident = {};
  if (last >= 2) {
    var values = sh.getRange(2, 1, last - 1, PURCHASE_COLUMNS.length).getValues();
    var iPrice = PURCHASE_COLUMNS.indexOf('price_rappen');
    var iName = PURCHASE_COLUMNS.indexOf('resident_name');
    var iVoid = PURCHASE_COLUMNS.indexOf('voided_at');
    for (var i = 0; i < values.length; i++) {
      if (values[i][iVoid]) continue;
      var who = cleanText_(values[i][iName], 23) || '?';
      if (!byResident[who]) byResident[who] = {name: who, drinks: 0, total_rappen: 0};
      byResident[who].drinks++;
      byResident[who].total_rappen += Number(values[i][iPrice]) || 0;
    }
  }
  return Object.keys(byResident).map(function(k) { return byResident[k]; });
}
