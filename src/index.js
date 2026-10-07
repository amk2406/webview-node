'use strict';

const { WebView } = require('./WebView');
const { EventEmitter } = require('events');

/**
 * Global registry of open windows + app-level events
 */
class WebViewApp extends EventEmitter {
  constructor() {
    super();
    this._windows = new Map(); // id → WebView
    this._nextId = 1;
  }

  /** @internal */
  _register(win) {
    const id = this._nextId++;
    this._windows.set(id, win);
    win._id = id;
    return id;
  }

  /** @internal */
  _unregister(id) {
    this._windows.delete(id);
    if (this._windows.size === 0) {
      this.emit('all-closed');
    }
  }

  getAllWindows() {
    return Array.from(this._windows.values());
  }

  getFocusedWindow() {
    for (const win of this._windows.values()) {
      if (win.isFocused()) return win;
    }
    return null;
  }

  closeAll(options = {}) {
    for (const win of this.getAllWindows()) {
      win.close(options);
    }
  }
}

const app = new WebViewApp();

module.exports = {
  WebView,
  app,
};
