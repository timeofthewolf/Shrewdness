const nf = new Intl.NumberFormat("en-US");

export function compact(n) {
  const a = Math.abs(n);
  if (a >= 1e9) return (n / 1e9).toFixed(1) + "B";
  if (a >= 1e6) return (n / 1e6).toFixed(1) + "M";
  if (a >= 10000) return (n / 1e3).toFixed(1) + "K";
  return nf.format(Math.round(n));
}
