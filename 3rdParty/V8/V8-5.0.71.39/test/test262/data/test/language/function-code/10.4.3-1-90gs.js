// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 10.4.3-1-90gs
description: >
    Strict - checking 'this' from a global scope (non-strict function
    declaration called by strict Function.prototype.call())
flags: [noStrict]
includes: [fnGlobalObject.js]
---*/

function f() { return this===fnGlobalObject();};
if (! ((function () {"use strict"; return f.call();})())){
    throw "'this' had incorrect value!";
}