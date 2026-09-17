/**
 * Input validation. Everything the terminal or the management page sends is
 * treated as untrusted: the device is on a kitchen wall and its token could
 * leak, so the server never relies on the client having checked anything.
 */

/** Trims, removes control characters, and bounds the length. */
function cleanText_(value, maxChars) {
  if (value === null || value === undefined) return '';
  var s = String(value).replace(/[\u0000-\u001F\u007F]/g, '').trim();
  if (s.length > maxChars) s = s.substring(0, maxChars);
  return s;
}

/**
 * Validates an EAN-13, EAN-8 or UPC-A check digit. An empty barcode is allowed:
 * products added at the terminal have no barcode until someone scans one.
 */
function isValidBarcode_(code) {
  if (!code) return true;
  if (!/^[0-9]+$/.test(code)) return false;
  if (code.length !== 8 && code.length !== 12 && code.length !== 13) return false;

  var sum = 0;
  // Weights alternate 3 and 1 counting leftwards from the check digit.
  for (var i = code.length - 2; i >= 0; i--) {
    var digit = parseInt(code.charAt(i), 10);
    var fromRight = code.length - 2 - i;
    sum += digit * (fromRight % 2 === 0 ? 3 : 1);
  }
  var expected = (10 - (sum % 10)) % 10;
  return expected === parseInt(code.charAt(code.length - 1), 10);
}

/** Integer rappen only; rejects floats, negatives and absurd values. */
function cleanPrice_(value) {
  var n = Number(value);
  if (!isFinite(n) || Math.floor(n) !== n) return null;
  if (n < 0 || n > LIMITS.maxPriceRappen) return null;
  return n;
}

/**
 * Only https, and only hosts the terminal is willing to fetch from. Open Food
 * Facts for catalogue photos, and Google's user content host so a beer it has no
 * picture for can be given one by hand.
 *
 * Keep this in step with api_protocol::image_source_allowed on the device: the
 * device checks again for itself, so a mismatch shows up as a photo that saves
 * here and never appears there.
 */
function cleanImageUrl_(value) {
  var s = cleanText_(value, LIMITS.maxImageUrlChars);
  if (!s) return '';
  if (/^https:[/][/]([a-z0-9-]+[.])*openfoodfacts[.]org[/]/i.test(s)) return s;
  if (/^https:[/][/]([a-z0-9-]+[.])*googleusercontent[.]com[/]/i.test(s)) return s;
  return '';
}

/**
 * Compares two secrets without leaking a difference through early exit. Apps
 * Script gives no constant-time primitive; this is the practical approximation.
 */
function secretEquals_(a, b) {
  a = String(a || '');
  b = String(b || '');
  if (a.length !== b.length) return false;
  var diff = 0;
  for (var i = 0; i < a.length; i++) {
    diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return diff === 0;
}

function requireDeviceToken_(request) {
  var expected = props_().getProperty('DEVICE_TOKEN');
  if (!expected) throw new Error('DEVICE_TOKEN ist nicht gesetzt');
  if (!secretEquals_(request.token, expected)) throw new Error('Token abgelehnt');
}

/**
 * The management page runs under the caller's own Google account, so there is a
 * real identity to check. On the anonymous device deployment this is empty,
 * which is exactly what must be refused.
 */
function requireAdmin_() {
  var email = Session.getActiveUser().getEmail();
  if (!email) throw new Error('Nicht angemeldet');
  var allowed = (props_().getProperty('ADMIN_EMAILS') || '')
                    .split(',')
                    .map(function(s) { return s.trim().toLowerCase(); })
                    .filter(function(s) { return s.length > 0; });
  if (allowed.length === 0) throw new Error('ADMIN_EMAILS ist nicht gesetzt');
  if (allowed.indexOf(email.toLowerCase()) === -1) {
    throw new Error('Kein Zugriff fuer ' + email);
  }
  return email;
}
