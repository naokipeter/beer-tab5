/**
 * Configuration and sheet access.
 *
 * Nothing secret lives in this file. The device token, the admin allowlist and
 * the spreadsheet id are read from Script Properties, which are not part of the
 * source and are not visible to anyone who only has the web app URL.
 *
 * Set them under Project Settings -> Script Properties:
 *   DEVICE_TOKEN    the shared secret the terminal sends; see secrets.h
 *   ADMIN_EMAILS    comma-separated Google accounts allowed to manage products
 *   SPREADSHEET_ID  optional; defaults to the container spreadsheet
 */

var SHEET_PRODUCTS = 'Products';
var SHEET_PURCHASES = 'Purchases';
var SHEET_RESIDENTS = 'Residents';

var PRODUCT_COLUMNS =
    ['barcode', 'name', 'price_rappen', 'free', 'image_url', 'active', 'updated_at'];
var PURCHASE_COLUMNS = [
  'transaction_id', 'timestamp', 'barcode', 'product_name', 'price_rappen', 'free',
  'resident_id', 'resident_name', 'device_id', 'voided_at'
];
var RESIDENT_COLUMNS = ['resident_id', 'name', 'active'];

/** Device-side limits. Keep in step with settings.h and catalog_codec. */
var LIMITS = {
  maxActiveProducts: 8,
  maxResidents: 11,
  maxNameChars: 39,
  maxBarcodeChars: 13,
  maxImageUrlChars: 255,
  maxPriceRappen: 99999
};

function props_() {
  return PropertiesService.getScriptProperties();
}

function spreadsheet_() {
  var id = props_().getProperty('SPREADSHEET_ID');
  if (id) return SpreadsheetApp.openById(id);
  var active = SpreadsheetApp.getActiveSpreadsheet();
  if (!active) {
    throw new Error('No spreadsheet. Set SPREADSHEET_ID in Script Properties.');
  }
  return active;
}

/** Returns the sheet, creating it with its header row if missing. */
function sheet_(name, columns) {
  var ss = spreadsheet_();
  var sh = ss.getSheetByName(name);
  if (!sh) {
    sh = ss.insertSheet(name);
    sh.getRange(1, 1, 1, columns.length).setValues([columns]).setFontWeight('bold');
    sh.setFrozenRows(1);
  }
  return sh;
}

function productsSheet_() { return sheet_(SHEET_PRODUCTS, PRODUCT_COLUMNS); }
function purchasesSheet_() { return sheet_(SHEET_PURCHASES, PURCHASE_COLUMNS); }
function residentsSheet_() { return sheet_(SHEET_RESIDENTS, RESIDENT_COLUMNS); }

/**
 * The catalog revision. Bumped by every product write so the terminal can skip
 * downloading a catalog it already has. Kept in Script Properties rather than a
 * cell: it is read on every sync and never needs to be human-edited.
 */
function revision_() {
  var v = parseInt(props_().getProperty('REVISION') || '0', 10);
  return isNaN(v) ? 0 : v;
}

function bumpRevision_() {
  var next = revision_() + 1;
  props_().setProperty('REVISION', String(next));
  return next;
}
