/**
 * projectLock.ts — In-process per-key serial queue.
 *
 * Single Node process serves all requests on this server (44.md §3.3),
 * so a Map of pending Promises is sufficient — no filesystem flock needed.
 *
 * Two requests targeting the same key wait for each other; different keys
 * proceed in parallel. The map entry is reset to `undefined` once the chain
 * settles to avoid unbounded growth from rare keys.
 */

const inflight = new Map<string, Promise<unknown>>();

export async function withLock<T>(key: string, fn: () => Promise<T>): Promise<T> {
  const previous = inflight.get(key);
  // Start the new task once the previous one settles. Catching prevents a
  // failed previous task from rejecting waiters before they get to run.
  const next = (previous ? previous.catch(() => undefined) : Promise.resolve()).then(fn);
  inflight.set(key, next);
  try {
    return await next;
  } finally {
    // Only clear if we're still the head — a later request may have already
    // chained itself onto `next`.
    if (inflight.get(key) === next) {
      inflight.delete(key);
    }
  }
}
