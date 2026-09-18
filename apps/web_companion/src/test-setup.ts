// jsdom does not implement layout APIs that the components call freely.
if (!Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}
