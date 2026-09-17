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

/** Creates or updates a product from the management page. */
function adminSaveProduct(input) {
  requireAdmin_();
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    upsertProduct_(input);
  } finally {
    lock.releaseLock();
  }
  return adminLoad();
}

/** Price changes are separate from creation so the page can confirm them. */
function adminChangePrice(barcode, name, priceRappen, free) {
  requireAdmin_();
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) throw new Error('Server beschaeftigt');
  try {
    setProductPrice_(barcode, name, priceRappen, free);
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
    setProductActive_(barcode, name, active);
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

/** Consumption summary for the page; the device gets the same figures via sync. */
function adminSummary() {
  requireAdmin_();
  return consumptionSummary_().map(function(r) {
    return {name: r.name, drinks: r.drinks, total_rappen: r.total_rappen};
  });
}
