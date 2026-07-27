export const app = $state({
  isa: [],
  examples: [],
  offline: false,
});

export async function api(path, body) {
  const opts = body
    ? { method: "POST", body: JSON.stringify(body) }
    : undefined;
  let res;
  try {
    res = await fetch(path, opts);
  } catch (e) {
    app.offline = true;
    throw e;
  }
  app.offline = false;
  const data = await res.json().catch(() => ({}));
  if (!res.ok && data.error === undefined) throw new Error(`HTTP ${res.status}`);
  return data;
}

export function init() {
  api("/api/isa").then((ops) => (app.isa = ops)).catch(() => {});
  api("/api/examples").then((ex) => (app.examples = ex)).catch(() => {});
}
