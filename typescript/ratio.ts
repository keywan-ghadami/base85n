import { readFileSync, readdirSync } from "node:fs";
import { encode, decode } from "./src/index.js";
let tb = 0, tc = 0;
for (const f of readdirSync("../bench/corpus").sort()) {
  let d: Uint8Array;
  try { d = new Uint8Array(readFileSync(`../bench/corpus/${f}`)); } catch { continue; }
  if (d.length === 0) continue;
  const e = encode(d);
  const back = decode(e);
  if (back.length !== d.length || !back.every((b, i) => b === d[i])) { console.log("FAIL", f); process.exit(1); }
  tb += d.length; tc += e.length;
  console.log(f.padEnd(28), (e.length / d.length).toFixed(5));
}
console.log("TOTAL".padEnd(28), (tc / tb).toFixed(5));
