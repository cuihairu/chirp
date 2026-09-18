import { useSyncExternalStore } from 'react';

/**
 * Minimal external store: notify-driven, zero dependencies. The shape maps
 * one-to-one onto a future Dart port (a ChangeNotifier or a Stream).
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

/** React binding: re-renders only when this store's snapshot changes. */
export function useStoreValue<T>(store: Store<T>): T {
  return useSyncExternalStore(store.subscribe, store.get, store.get);
}

/**
 * Read-modify-write helper for stores whose state is an object: keeps call
 * sites honest about producing a new snapshot object.
 */
export function patch<T extends object>(store: Store<T>, changes: Partial<T>): void {
  store.set({ ...store.get(), ...changes });
}
