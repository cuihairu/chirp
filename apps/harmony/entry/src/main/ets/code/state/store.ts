/**
 * Minimal external store: notify-driven, zero dependencies. Ported from
 * apps/web_companion src/state/store.ts — the React binding
 * (useSyncExternalStore/useStoreValue) is dropped here; ArkUI pages
 * subscribe via store.subscribe in aboutToAppear and re-render from
 * snapshots, keeping the set()/get() snapshot contract identical.
 */
export interface Store<T> {
  get(): T;
  set(next: T | ((prev: T) => T)): void;
  subscribe(listener: () => void): () => void;
}

export function createStore<T>(initial: T): Store<T> {
  let state = initial;
  const listeners = new Set<() => void>();
  return {
    get: () => state,
    set: (next) => {
      const value = typeof next === 'function' ? (next as (prev: T) => T)(state) : next;
      if (value === state) return;
      state = value;
      for (const listener of listeners) listener();
    },
    subscribe: (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

/**
 * Read-modify-write helper for stores whose state is an object: keeps call
 * sites honest about producing a new snapshot object.
 */
export function patch<T extends object>(store: Store<T>, changes: Partial<T>): void {
  store.set({ ...store.get(), ...changes });
}
