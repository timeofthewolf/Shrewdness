export function parseGenes(input) {
  if (Array.isArray(input)) return input.map((x) => x | 0);
  const out = [];
  const re = /-?\d+/g;
  let m;
  while ((m = re.exec(input || ""))) out.push(parseInt(m[0], 10));
  return out;
}

const FAMILIES = [
  { name: "push", color: "var(--s1)", ops: ["PUSHI", "PUSH0", "PUSH1", "PUSH2", "PUSH10", "PUSHN1"] },
  { name: "stack", color: "var(--muted)", ops: ["POP", "DUP", "SWAP", "OVER"] },
  { name: "arith", color: "var(--s2)", ops: ["ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "INC", "DEC"] },
  { name: "logic", color: "var(--s5)", ops: ["LT", "GT", "EQ", "NEQ", "NOT", "AND", "OR"] },
  { name: "register", color: "var(--s7)", ops: ["LD0", "LD1", "LD2", "LD3", "ST0", "ST1", "ST2", "ST3"] },
  { name: "memory", color: "var(--s4)", ops: ["MLOAD", "MSTORE", "MSIZE"] },
  { name: "control", color: "var(--s6)", ops: ["IF", "ELSE", "DO", "END", "BREAK"] },
  { name: "call", color: "var(--s3)", ops: ["PROC", "CALL", "RET"] },
  { name: "io", color: "var(--s8)", ops: ["OUT", "OUTNUM", "IN", "RAND"] },
  { name: "reproduction", color: "var(--accent)", ops: ["GLEN", "GREAD", "EMIT", "CLEN", "CREAD", "CWRITE", "SPAWN"] },
  { name: "nop", color: "color-mix(in srgb, var(--muted) 35%, transparent)", ops: ["NOP"] },
];
const LITERAL = { name: "literal", color: "color-mix(in srgb, var(--ink) 26%, transparent)" };

const OP_FAMILY = (() => {
  const m = new Map();
  for (const f of FAMILIES) for (const op of f.ops) m.set(op, f);
  return m;
})();

export const GENE_LEGEND = [...FAMILIES, LITERAL];

export function geneCells(genes, isa) {
  const n = isa?.length || 0;
  const nameOf = (g) => (n ? isa[(((g % n) + n) % n)] : null);
  const cells = [];
  for (let i = 0; i < genes.length; ) {
    const op = nameOf(genes[i]);
    const fam = (op && OP_FAMILY.get(op.name)) || { name: "?", color: "var(--muted)" };
    cells.push({ g: genes[i], i, kind: "op", name: op?.name ?? "?", family: fam.name, color: fam.color });
    if (op && op.size === 2 && i + 1 < genes.length) {
      cells.push({ g: genes[i + 1], i: i + 1, kind: "lit", name: "literal", family: "literal", color: LITERAL.color });
      i += 2;
    } else {
      i += 1;
    }
  }
  return cells;
}

export function geneDiff(parent, child) {
  const changed = new Set();
  const max = Math.max(parent.length, child.length);
  for (let i = 0; i < max; i++) if (parent[i] !== child[i]) changed.add(i);
  return changed;
}
