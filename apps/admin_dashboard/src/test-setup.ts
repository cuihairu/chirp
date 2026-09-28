import { configure } from '@testing-library/dom';

// jsdom does not implement layout APIs that the components call freely.
if (!Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}

// recharts' ResponsiveContainer measures its parent through ResizeObserver;
// jsdom has neither, so the charts would never mount. The stub reports a
// fixed viewport on the next macrotask (fake timers advance it in tests).
class ResizeObserverStub {
  private callback: ResizeObserverCallback;
  constructor(callback: ResizeObserverCallback) {
    this.callback = callback;
  }
  observe(target: Element) {
    setTimeout(() => {
      this.callback(
        [
          {
            target,
            contentRect: {
              width: 600, height: 300, x: 0, y: 0, top: 0, left: 0, bottom: 0, right: 0,
            } as DOMRectReadOnly,
          } as ResizeObserverEntry,
        ],
        this as unknown as ResizeObserver,
      );
    }, 0);
  }
  unobserve() {}
  disconnect() {}
}
if (!globalThis.ResizeObserver) {
  globalThis.ResizeObserver = ResizeObserverStub as unknown as typeof ResizeObserver;
}

// waitFor's default 1s budget assumes an idle machine; on a loaded dev box a
// single render can exceed it and the same test flakes run to run. The five
// second budget still fails fast on a real regression.
configure({ asyncUtilTimeout: 5000 });
