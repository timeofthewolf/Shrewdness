const bridge = typeof window !== "undefined" ? window.shrewdness : null;

export const isDesktop = !!bridge?.desktop;

export function detachTab(key, x, y) {
  if (!isDesktop) return false;
  try {
    bridge.detachTab({ key, x, y });
    return true;
  } catch {
    return false;
  }
}
