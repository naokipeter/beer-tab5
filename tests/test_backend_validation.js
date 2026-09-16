// Host test for the Apps Script validators. These are the server's only defence
// against a leaked device token, so they are worth testing away from Google.
// Run with tools/test.sh; needs Node only.
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const dir = path.join(__dirname, '..', 'backend', 'apps-script');
const sandbox = {console};
vm.createContext(sandbox);
// Config.gs only defines constants at load time; nothing calls into Apps Script
// services until a request arrives, so these two files load standalone.
for (const file of ['Config.gs', 'Validate.gs']) {
  vm.runInContext(fs.readFileSync(path.join(dir, file), 'utf8'), sandbox, {filename: file});
}

let failures = 0;
function check(ok, what) {
  if (ok) console.log('  ok ' + what);
  else { console.log('FAIL ' + what); failures++; }
}

console.log('barcode check digits');
// A widely used valid EAN-13, and the seeds the firmware ships with.
check(sandbox.isValidBarcode_('4006381333931'), 'a known-good EAN-13 passes');
check(sandbox.isValidBarcode_('7610807000016'), 'the firmware seed passes');
check(!sandbox.isValidBarcode_('7610807000019'), 'a wrong check digit fails');
check(sandbox.isValidBarcode_('96385074'), 'a known-good EAN-8 passes');
check(!sandbox.isValidBarcode_('96385075'), 'a wrong EAN-8 check digit fails');
check(sandbox.isValidBarcode_('036000291452'), 'a known-good UPC-A passes');
check(sandbox.isValidBarcode_(''), 'an empty barcode is allowed');
check(!sandbox.isValidBarcode_('12345'), 'a wrong length fails');
check(!sandbox.isValidBarcode_('761080700001a'), 'a non-digit fails');

console.log('');
console.log('prices');
check(sandbox.cleanPrice_(180) === 180, 'an integer passes through');
check(sandbox.cleanPrice_(0) === 0, 'zero is allowed for free items');
check(sandbox.cleanPrice_(2.4) === null, 'a float is rejected');
check(sandbox.cleanPrice_(-1) === null, 'a negative is rejected');
check(sandbox.cleanPrice_(100000) === null, 'an absurd price is rejected');
check(sandbox.cleanPrice_('abc') === null, 'a non-number is rejected');
check(sandbox.cleanPrice_('180') === 180, 'a numeric string is accepted');

console.log('');
console.log('text');
check(sandbox.cleanText_('  Feldschloesschen  ', 39) === 'Feldschloesschen',
      'trims surrounding space');
check(sandbox.cleanText_('Bräu', 39) === 'Bräu', 'keeps umlauts');
check(sandbox.cleanText_('a\u0007b\u001bc', 39) === 'abc',
      'strips control characters');
check(sandbox.cleanText_('N'.repeat(100), 39).length === 39, 'bounds the length');
check(sandbox.cleanText_(null, 39) === '', 'null becomes empty');

console.log('');
console.log('image urls');
check(sandbox.cleanImageUrl_('https://images.openfoodfacts.org/x/front.400.jpg') !== '',
      'an Open Food Facts https url is kept');
check(sandbox.cleanImageUrl_('http://images.openfoodfacts.org/x.jpg') === '',
      'plain http is rejected');
check(sandbox.cleanImageUrl_('https://evil.example.com/x.jpg') === '',
      'another host is rejected');
check(sandbox.cleanImageUrl_('https://openfoodfacts.org.evil.com/x.jpg') === '',
      'a lookalike host is rejected');

console.log('');
console.log('token comparison');
check(sandbox.secretEquals_('abc', 'abc'), 'equal secrets match');
check(!sandbox.secretEquals_('abc', 'abd'), 'different secrets do not match');
check(!sandbox.secretEquals_('abc', 'ab'), 'a prefix does not match');
check(!sandbox.secretEquals_('', 'abc'), 'an empty token does not match');
check(!sandbox.secretEquals_(undefined, 'abc'), 'a missing token does not match');

console.log(failures ? ('') + failures + ' failure(s)' : 'all checks passed');
process.exit(failures ? 1 : 0);
