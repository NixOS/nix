// result:
// json-arrays is 10% faster on nixpkgs/pkgs/top-level/all-packages.nix
// -> not really worth the trouble

const cp = require('child_process');
const fs = require('fs');

const printResult = 0;
const writeResult = 1;
const showTodoImplement = 1;
const inputFile = '/nix/store/v0xwj556c69yppjzylz2diqk66vliswb-nixos-20.09.3857.b2a189a8618/nixos/nixpkgs/pkgs/top-level/all-packages.nix';
const showJson = 1;

const maxLen = {
  format: 12,
  label: 12,
}

const dtObj = {};
function dt(t1, t2, label) {
  // global: outputFormat
  var dtSec = t2[0] - t1[0];
  var dtNsec = t2[1] - t1[1];
  var dt = (dtSec + dtNsec*1E-9);
  console.log(`${outputFormat.padEnd(maxLen.format, ' ')} ${label.padEnd(maxLen.label, ' ')} ${dt.toFixed(9)} sec`);
  (outputFormat in dtObj) || (dtObj[outputFormat] = []);
  dtObj[outputFormat].push(dt);
}

const stObj = {};
function st() {
  // global: outputFormat
  var dt = dtObj[outputFormat].reduce((sum, dt) => sum + dt, 0);
  stObj[outputFormat] = dt;
  var label = 'sum';
  console.log(`${outputFormat.padEnd(maxLen.format, ' ')} ${label.padEnd(maxLen.label, ' ')} ${dt.toFixed(9)} sec`)
}

function showAttrPath(path) {
  // TODO build double quoted string with dollar curly exprs
  // expected: bionic = assert stdenv.hostPlatform.useAndroidPrebuilt; pkgs."androidndkPkgs_${stdenv.hostPlatform.ndkVer}".libraries;
  // actual: bionic = (assert ((stdenv)."hostPlatform"."useAndroidPrebuilt"); ((pkgs).(("androidndkPkgs_") + ((stdenv)."hostPlatform"."ndkVer"))."libraries"));
  //                                                                                  ^ syntax error: unexpected '(', expecting ID or OR_KW or DOLLAR_CURLY or '"'
  // maybe syntax rules should be relaxed? -> result of expr must be string
  return path.map(p => {
    if (p.expr == undefined) return JSON.stringify(p.symbol);
    else return `(${walkJson(p.expr)})`;
  }).join('.');
}
// cat src/libexpr/nixexpr-node-types.def | grep ^ADD_TYPE | cut -d'(' -f2 | cut -d')' -f1 | while read x; do echo "$x: node => {},"; done
const walkJsonHandler = {
  ExprLambda: node => {
    res = '';
    if (node.matchAttrs) {
      res += '{ ';
      var first = true;
      for (const formal of node.formals) {
        if (first) first = false; else res += ', ';
        res += formal.name;
        if (formal.default) res += `? (${walkJson(formal.default)})`;
      }
      if (node.ellipsis) {
        if (!first) res += ', ';
        res += '...';
      }
      res += ' }';
    }
    if (node.arg) {
      if (node.matchAttrs) res += ' @ ';
      res += node.arg;
    }
    res += `: (${walkJson(node.body)})`;
    return res;
  },
  ExprSet: node => {},
  ExprList: node => { return '[\n' + node.items.map(i => `(${walkJson(i)})`).join('\n') + '\n]'; },
  ExprAttrs: node => {
    let res = '';
    if (node.recursive) res += 'rec ';
    res += '{';
    // same as ExprLet
    for (const attr of node.attrs) {
      if (attr.inherited) res += `\ninherit ${attr.name};`;
      else res += `\n${attr.name} = (${walkJson(attr.value)});`;
    }
    if (node.attrs.length > 0) res += '\n';
    res += '}';
    return res;
  },
  ExprString: node => { return JSON.stringify(node.value); },
  ExprInt: node => { return node.value; },
  ExprFloat: node => { return node.value; },
  ExprPath: node => { return JSON.stringify(node.value).slice(1, -1); },
  ExprBoolean: node => {},
  ExprNull: node => {},
  ExprLet: node => {
    let res = 'let';
    for (const attr of node.attrs) {
      if (attr.inherited) res += `\ninherit ${attr.name}; `;
      else res += `\n${attr.name} = ${walkJson(attr.value)}; `;
    }
    res += `\nin (${walkJson(node.body)})`;
    return res;
  },
  ExprWith: node => {
    return `with ${walkJson(node.set)}; (${walkJson(node.body)})`
  },
  ExprIf: node => { return `if (${walkJson(node.cond)}) then (${walkJson(node.then)}) else (${walkJson(node.else)})`; },
  ExprAssert: node => { return `assert (${walkJson(node.cond)}); (${walkJson(node.body)})`; },
  ExprVar: node => { return node.name; },
  ExprSelect: node => { return `${walkJson(node.set)}.${showAttrPath(node.attr)}` + (node.default == undefined ? '' : ` or (${walkJson(node.default)})`); },
  ExprApp: node => { return `(${walkJson(node.op1)}) (${walkJson(node.op2)})`; },
  ExprOpNeg: node => {}, // not used
  ExprOpHasAttr: node => { return `(${walkJson(node.set)}) ? ${showAttrPath(node.attr)}`; },
  ExprOpConcatLists: node => { return `(${walkJson(node.op1)}) ++ (${walkJson(node.op2)})`; },
  ExprOpMul: node => {}, // not used
  ExprOpDiv: node => {}, // not used
  ExprConcatStrings: node => { return node.strings.map(s => walkJson(s)).join(' + '); },
  ExprOpAdd: node => {}, // not used
  ExprOpSub: node => {}, // not used
  ExprOpNot: node => { return `!(${walkJson(node.expr)})`; },
  ExprOpUpdate: node => { return `(${walkJson(node.op1)}) // (${walkJson(node.op2)})`; },
  ExprOpLt: node => {}, // not used
  ExprOpLte: node => {}, // not used
  ExprOpGt: node => {}, // not used
  ExprOpGte: node => {}, // not used
  ExprOpEq: node => { return `(${walkJson(node.op1)}) == (${walkJson(node.op2)})`; },
  ExprOpNEq: node => { return `(${walkJson(node.op1)}) != (${walkJson(node.op2)})`; },
  ExprOpAnd: node => { return `(${walkJson(node.op1)}) && (${walkJson(node.op2)})`; },
  ExprOpOr: node => { return `(${walkJson(node.op1)}) || (${walkJson(node.op2)})`; },
  ExprOpImpl: node => { return `(${walkJson(node.op1)}) -> (${walkJson(node.op2)})`; },
  ExprPos: node => {},
  Comment: node => {}, // not used
};
function walkJson(node) {
  if (walkJsonHandler[node.type] == undefined) {
    console.log(`FIXME missing type ${node.type}. did you confuse walkJson and walkJsonArrays?`);
    console.log(new Error().stack);
    //console.log(`node:`); console.dir(node);
    return '';
  }
  const res = walkJsonHandler[node.type](node);
  if (res == undefined && showTodoImplement) {
    console.log(`TODO implement type ${node.type}`);
  }
  return res;
}



function showAttrPathArrays(path) {
  // TODO build double quoted string with dollar curly exprs
  return path.map(p => {
    if (p[0] == 0) return JSON.stringify(p[1]); // symbol
    else return `(${walkJsonArrays(p[1])})`; // expr
  }).join('.');
}
// cat src/libexpr/nixexpr-node-types.def | grep ^ADD_TYPE | cut -d'(' -f2 | cut -d')' -f1 | while read x; do echo "function $x (node) {},"; done
const walkJsonArraysHandler = [
  null, // not used
  function ExprLambda (node) {
    res = '';
    if (node[1] == 1) { // node[1] = node.matchAttrs
      res += '{ ';
      var first = true;
      for (const formal of node[2]) { // node[2] = node.formals
        if (first) first = false; else res += ', ';
        res += formal[0]; // formal.name
        if (formal[1]) res += `? (${walkJsonArrays(formal[1])})`; // formal.default
      }
      if (node[3]) { // node.ellipsis
        if (!first) res += ', ';
        res += '...';
      }
      res += ' }';
    }
    if (node[4] !== 0) { // node.arg
      if (node[1] == 1) res += ' @ ';
      res += node[4]; // node.arg
    }
    res += `: (${walkJsonArrays(node[5])})`; // node.body
    return res;
  },  function ExprSet (node) {},
  function ExprList (node) { return '[ ' + node[1].map(i => `(${walkJsonArrays(i)})`).join(' ') + ' ]'; },
  function ExprAttrs (node) {
    let res = '';
    if (node[1] == 1) res += 'rec ';
    res += '{';
    // same as ExprLet
    for (const attr of node[2]) {
      if (attr[0] == 1) res += `\ninherit ${attr[1]};`;
      else res += `\n${attr[1]} = (${walkJsonArrays(attr[2])});`;
    }
    if (node[2].length > 0) res += '\n';
    res += '}';
    return res;
  },
  function ExprString (node) { return JSON.stringify(node[1]); },
  function ExprInt (node) { return node[1]; },
  function ExprFloat (node) { return node[1]; },
  function ExprPath (node) { return JSON.stringify(node[1]).slice(1, -1); },
  function ExprBoolean (node) {},
  function ExprNull (node) {},
  function ExprLet (node) {
    let res = 'let';
    for (const attr of node[1]) {
      if (attr[0] == 1) res += `\ninherit ${attr[1]}; `;
      else res += `\n${attr[1]} = (${walkJsonArrays(attr[2])}); `;
    }
    res += `\nin (${walkJsonArrays(node[2])})`;
    return res;
  },
  function ExprWith (node) {
    return `with (${walkJsonArrays(node[1])}); (${walkJsonArrays(node[2])})`
  },
  function ExprIf (node) { return `if (${walkJsonArrays(node[1])}) then (${walkJsonArrays(node[2])}) else (${walkJsonArrays(node[3])})`; },
  function ExprAssert (node) { return `assert (${walkJsonArrays(node[1])}); (${walkJsonArrays(node[2])})`; },
  function ExprVar (node) { return node[1]; },
  function ExprSelect (node) { return `(${walkJsonArrays(node[1])}).${showAttrPathArrays(node[2])}` + (node[3] == undefined ? '' : ` or (${walkJsonArrays(node[3])})`); },
  function ExprApp (node) { return `(${walkJsonArrays(node[1])}) (${walkJsonArrays(node[2])})`; },
  function ExprOpNeg (node) {}, // not used
  function ExprOpHasAttr (node) { return `(${walkJsonArrays(node[1])}) ? ${showAttrPathArrays(node[2])}`; },
  function ExprOpConcatLists (node) { return `(${walkJsonArrays(node[1])}) ++ (${walkJsonArrays(node[2])})`; },
  function ExprOpMul (node) {}, // not used
  function ExprOpDiv (node) {}, // not used
  function ExprConcatStrings (node) { return node[1].map(s => `(${walkJsonArrays(s)})`).join(' + '); },
  function ExprOpAdd (node) {}, // not used
  function ExprOpSub (node) {}, // not used
  function ExprOpNot (node) { return `!(${walkJsonArrays(node[1])})`; },
  function ExprOpUpdate (node) { return `(${walkJsonArrays(node[1])}) // (${walkJsonArrays(node[2])})`; },
  function ExprOpLt (node) {}, // not used
  function ExprOpLte (node) {}, // not used
  function ExprOpGt (node) {}, // not used
  function ExprOpGte (node) {}, // not used
  function ExprOpEq (node) { return `(${walkJsonArrays(node[1])}) == (${walkJsonArrays(node[2])})`; },
  function ExprOpNEq (node) { return `(${walkJsonArrays(node[1])}) != (${walkJsonArrays(node[2])})`; },
  function ExprOpAnd (node) { return `(${walkJsonArrays(node[1])}) && (${walkJsonArrays(node[2])})`; },
  function ExprOpOr (node) { return `(${walkJsonArrays(node[1])}) || (${walkJsonArrays(node[2])})`; },
  function ExprOpImpl (node) { return `(${walkJsonArrays(node[1])}) -> (${walkJsonArrays(node[2])})`; },
  function ExprPos (node) {},
  function Comment (node) {}, // not used
];
function walkJsonArrays(node) {
  if (walkJsonArraysHandler[node[0]] == undefined) {
    console.log(`FIXME missing type id ${node[0]}. did you confuse walkJson and walkJsonArrays?`);
    console.log(new Error().stack);
    //console.log(`node:`); console.dir(node);
    return '';
  }
  const res = walkJsonArraysHandler[node[0]](node);
  if (res == undefined && showTodoImplement) {
    //console.log(`TODO implement type id ${node[0]}`);
  }
  return res;
}

///////////////////

var outputFormat = 'json-arrays';

var execOptions = { encoding: 'utf8', maxBuffer: 1/0 };

var t1 = process.hrtime();
var json = cp.execSync(`./nix-instantiate --parse --${outputFormat} ${inputFile}`, execOptions);
var t2 = process.hrtime();
dt(t1, t2, `generate`);

var t1 = process.hrtime();
var obj = JSON.parse(json);
var t2 = process.hrtime();
dt(t1, t2, `JSON.parse`);
//console.dir(obj);

var t1 = process.hrtime();
var res = walkJsonArrays(obj);
var t2 = process.hrtime();
dt(t1, t2, `walkJson`);
if (printResult) console.log(res);
if (writeResult) {
  var of = `benchmark-parse-json-vs-json-arrays.js.out.${outputFormat}.nix`;
  fs.writeFileSync(of, res, 'utf8');
  console.log(`done ${of}`);
  // validate syntax
  cp.execSync(`./nix-instantiate --parse ${of}`, { stdio: 'inherit' });
}

st();

/////////////////
console.log();
/////////////////

if (showJson) {

var outputFormat = 'json';

var t1 = process.hrtime();
var json = cp.execSync(`./nix-instantiate --parse --${outputFormat} ${inputFile}`, execOptions);
var t2 = process.hrtime();
dt(t1, t2, `generate`);

var t1 = process.hrtime();
var obj = JSON.parse(json);
var t2 = process.hrtime();
dt(t1, t2, 'JSON.parse');
//console.dir(obj);

var t1 = process.hrtime();
var res = walkJson(obj);
var t2 = process.hrtime();
dt(t1, t2, `walkJson`);
if (printResult) console.log(res);
if (writeResult) {
  var of = `benchmark-parse-json-vs-json-arrays.js.out.${outputFormat}.nix`;
  fs.writeFileSync(of, res, 'utf8');
  console.log(`done ${of}`);
  // validate syntax
  cp.execSync(`./nix-instantiate --parse ${of}`, { stdio: 'inherit' });
}

st();

}

console.log(`time of json vs json-arrays: +${((stObj['json'] / stObj['json-arrays'] - 1)*100).toFixed(1)} %`)
console.log(`time of json-arrays vs json: -${((1 - stObj['json-arrays'] / stObj['json'])*100).toFixed(1)} %`)

// low-res timer
/*
var t1 = new Date().getTime();
var obj = JSON.parse(json);
var t2 = new Date().getTime();
var dt = t2 - t1;
console.log(`${outputFormat.padEnd(12, ' ')} JSON.parse: dt = ${dt / 1000} sec`)
*/
