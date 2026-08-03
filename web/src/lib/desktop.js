const bridge = typeof window !== "undefined" ? window.shrewdness : null;

export const isDesktop = !!bridge?.desktop;

export async function detachTab(key, x, y, last) {
  if (!isDesktop) return "ignored";
  try {
    return await bridge.detachTab({ key, x, y, last });
  } catch {
    return "ignored";
  }
}

export function onAdoptTab(fn) {
  if (!isDesktop) return;
  try {
    bridge.onAdoptTab(fn);
  } catch {}
}
