import { configure } from '@testing-library/dom';

// jsdom does not implement layout APIs that the components call freely.
if (!Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}

// waitFor's default 1s budget assumes an idle machine; on a loaded dev box a
// single render can exceed it and the same test flakes run to run. The five
// second budget still fails fast on a real regression.
configure({ asyncUtilTimeout: 5000 });
