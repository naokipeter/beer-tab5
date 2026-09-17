/**
 * The device-facing API. One endpoint, POST, JSON in and out, distinguished by
 * "action". The contract is fixed in docs/milestone-7.md and implemented on the
 * device by api_protocol.
 *
 * Every response is {ok: true, ...} or {ok: false, error: "..."}. A transport
 * that answered at all is a 200; failures are carried in the body, because Apps
 * Script cannot set arbitrary status codes.
 */

function jsonOut_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
      .setMimeType(ContentService.MimeType.JSON);
}

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonOut_({ok: false, error: 'Leerer Request'});
    }
    var request = JSON.parse(e.postData.contents);
    requireDeviceToken_(request);

    switch (request.action) {
      case 'sync':
        return jsonOut_(handleSync_(request));
      case 'recordPurchase':
        return jsonOut_(handleRecordPurchase_(request));
      case 'voidPurchase':
        return jsonOut_(handleVoidPurchase_(request));
      case 'mySummary':
        return jsonOut_(handleMySummary_(request));
      case 'createProduct':
        return jsonOut_(handleCreateProduct_(request));
      case 'setActive':
        return jsonOut_(handleSetActive_(request));
      case 'changePrice':
        return jsonOut_(handleChangePrice_(request));
      default:
        return jsonOut_({ok: false, error: 'Unbekannte Aktion'});
    }
  } catch (err) {
    // The message is shown on the terminal, so keep it short and free of stack
    // detail. The full error goes to the execution log.
    console.error(err);
    return jsonOut_({ok: false, error: String(err.message || err).substring(0, 60)});
  }
}

/**
 * Returns the catalog and the resident list in one round trip. When the caller
 * already has the current revision the lists are omitted: on a battery device
 * the association costs more than the payload, but the payload is not free.
 */
function handleSync_(request) {
  var rev = revision_();
  var since = parseInt(request.since, 10);

  // The summary goes out on every sync, unlike the catalog. It changes with
  // every purchase, while the revision only moves when a product is edited, so
  // gating it on `changed` would leave the terminal showing stale totals.
  var summary = consumptionSummary_();

  if (since === rev) {
    return {
      ok: true, revision: rev, changed: false, products: [], residents: [],
      summary: summary
    };
  }

  var products = readProducts_().filter(function(p) {
    // The terminal can only show so many; sending more would be rejected wholesale.
    return true;
  });

  var active = products.filter(function(p) { return p.active; });
  if (active.length > LIMITS.maxActiveProducts) {
    // Trim rather than let the device reject the entire response and keep a
    // stale catalog. The surplus stays archived from the terminal's point of view.
    var kept = 0;
    products = products.map(function(p) {
      if (!p.active) return p;
      kept++;
      if (kept > LIMITS.maxActiveProducts) {
        var copy = JSON.parse(JSON.stringify(p));
        copy.active = false;
        return copy;
      }
      return p;
    });
  }

  var residents = readResidents_().slice(0, LIMITS.maxResidents);
  return {
    ok: true, revision: rev, changed: true, products: products,
    residents: residents, summary: summary
  };
}

/** One resident's own consumption, per product. */
function handleMySummary_(request) {
  var residentId = cleanText_(request.resident_id, 11);
  if (!residentId) return {ok: false, error: 'Person fehlt'};
  var resident = findResident_(residentId);
  var rows = residentBreakdown_(residentId);
  var total = 0;
  var drinks = 0;
  for (var i = 0; i < rows.length; i++) {
    total += rows[i].total_rappen;
    drinks += rows[i].drinks;
  }
  return {
    ok: true,
    resident_name: resident ? resident.name : residentId,
    drinks: drinks,
    total_rappen: total,
    products: rows
  };
}

/**
 * A beer added at the fridge. Without this the product stayed on that one
 * terminal: the purchase reached Purchases, but Products never learned of it,
 * so the phone page could not price or archive it.
 */
function handleCreateProduct_(request) {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return {ok: false, retry: true, error: 'Server beschaeftigt'};
  try {
    upsertProduct_(request);
    return {ok: true};
  } finally {
    lock.releaseLock();
  }
}

/**
 * Shelving and archiving from the terminal. Deliberately open to anyone holding
 * the device token rather than an admin: this is the fridge changing, and the
 * household does it several times a week. Every change lands in the sheet with
 * a timestamp, which is where it can be reviewed.
 */
function handleSetActive_(request) {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return {ok: false, retry: true, error: 'Server beschaeftigt'};
  try {
    setProductActive_(request.barcode, request.name, request.active === true);
    return {ok: true};
  } finally {
    lock.releaseLock();
  }
}

function handleChangePrice_(request) {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return {ok: false, retry: true, error: 'Server beschaeftigt'};
  try {
    setProductPrice_(request.barcode, request.name, request.price_rappen,
                     request.free === true);
    return {ok: true};
  } finally {
    lock.releaseLock();
  }
}

function handleRecordPurchase_(request) {
  var transactionId = cleanText_(request.transaction_id, 64);
  if (!transactionId) return {ok: false, error: 'Transaktions-ID fehlt'};

  var barcode = cleanText_(request.barcode, LIMITS.maxBarcodeChars);
  if (!isValidBarcode_(barcode)) return {ok: false, error: 'Barcode ungueltig'};

  var name = cleanText_(request.name, LIMITS.maxNameChars);
  if (!name) return {ok: false, error: 'Produktname fehlt'};

  var free = request.free === true;
  var price = cleanPrice_(request.price_rappen);
  if (price === null) return {ok: false, error: 'Preis ungueltig'};
  if (free && price !== 0) return {ok: false, error: 'Gratis mit Preis'};
  if (!free && price === 0) return {ok: false, error: 'Preis fehlt'};

  var residentId = cleanText_(request.resident_id, 32);
  var resident = findResident_(residentId);
  if (!resident) return {ok: false, error: 'Unbekannte Person'};

  var deviceId = cleanText_(request.device, 32);

  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return {ok: false, error: 'Server beschaeftigt'};
  try {
    // The duplicate check and the append happen under one lock, and the check
    // reads the sheet rather than a cache, so a crash between append and
    // response still leaves exactly one row.
    if (findPurchaseRow_(transactionId) > 0) {
      return {ok: true, duplicate: true};
    }
    purchasesSheet_().appendRow([
      transactionId, new Date(), barcode, name, price, free, resident.resident_id,
      resident.name, deviceId, ''
    ]);
    return {ok: true, duplicate: false};
  } finally {
    lock.releaseLock();
  }
}

/**
 * Marks a purchase reversed rather than deleting the row, so the sheet keeps an
 * auditable trail. Idempotent: voiding an already-voided purchase succeeds.
 */
function handleVoidPurchase_(request) {
  var transactionId = cleanText_(request.transaction_id, 64);
  if (!transactionId) return {ok: false, error: 'Transaktions-ID fehlt'};

  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return {ok: false, error: 'Server beschaeftigt'};
  try {
    var row = findPurchaseRow_(transactionId);
    if (row <= 0) return {ok: false, error: 'Buchung nicht gefunden'};
    var sh = purchasesSheet_();
    var col = PURCHASE_COLUMNS.indexOf('voided_at') + 1;
    var current = sh.getRange(row, col).getValue();
    if (!current) sh.getRange(row, col).setValue(new Date());
    return {ok: true};
  } finally {
    lock.releaseLock();
  }
}
