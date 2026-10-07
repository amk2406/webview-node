'use strict';

const { EventEmitter } = require('events');
const path = require('path');

/**
 * @typedef {Object} WebViewOptions
 * @property {string}  [url]              - URL or file path to load
 * @property {string}  [title]            - Window title
 * @property {number}  [width=900]        - Initial width
 * @property {number}  [height=600]       - Initial height
 * @property {number}  [x]                - Initial X position
 * @property {number}  [y]                - Initial Y position
 * @property {number}  [minWidth]         - Minimum width
 * @property {number}  [minHeight]        - Minimum height
 * @property {number}  [maxWidth]         - Maximum width
 * @property {number}  [maxHeight]        - Maximum height
 * @property {string}  [icon]             - Path to .ico / .png or data URL
 * @property {boolean} [resizable=true]
 * @property {boolean} [maximizable=true]
 * @property {boolean} [minimizable=true]
 * @property {boolean} [closable=true]
 * @property {boolean} [center=true]      - Center on screen (ignored if x/y set)
 * @property {boolean} [frame=true]       - Show window frame
 * @property {boolean} [transparent=false]
 * @property {boolean} [alwaysOnTop=false]
 * @property {boolean} [fullscreen=false]
 * @property {string}  [backgroundColor="#ffffff"]
 * @property {boolean} [devTools=false]
 * @property {boolean} [windowsHide=false] - Hide the native host process window
 * @property {string}  [userDataFolder]   - WebView2 user data folder (absolute path)
 */

class WebView extends EventEmitter {
  /**
   * @param {WebViewOptions} options
   */
  constructor(options = {}) {
    super();

    this._options = {
      width: 900,
      height: 600,
      resizable: true,
      maximizable: true,
      minimizable: true,
      closable: true,
      center: true,
      frame: true,
      transparent: false,
      alwaysOnTop: false,
      fullscreen: false,
      backgroundColor: '#ffffff',
      devTools: false,
      windowsHide: false,
      ...options,
    };

    // Resolve relative file paths to absolute file:// URLs
    if (
      this._options.url &&
      !/^https?:\/\//i.test(this._options.url) &&
      !this._options.url.startsWith('file:') &&
      !this._options.url.startsWith('data:')
    ) {
      const abs = path.resolve(this._options.url);
      this._options.url = 'file:///' + abs.replace(/\\/g, '/');
    }

    // If explicit position is given, don't force center
    if (typeof this._options.x === 'number' || typeof this._options.y === 'number') {
      this._options.center = false;
    }

    this._id = null;
    this._ready = false;
    this._destroyed = false;
    this._shown = false;
    this._visible = false;
    this._focused = false;
    this._minimized = false;
    this._maximized = false;
    this._ipc = null;
    this._pendingUrl = null;
    this._pendingCalls = [];

    const { app } = require('./index');
    this._id = app._register(this);
  }

  /**
   * Create the native window and show it.
   * @returns {this}
   */
  show() {
    if (this._destroyed) {
      throw new Error('Window is destroyed');
    }
    if (this._shown) {
      this._send('show');
      this._visible = true;
      return this;
    }

    const { getSharedIpc } = require('./ipc');
    this._ipc = getSharedIpc({ windowsHide: this._options.windowsHide });
    this._setupIpc();

    this._ipc.send({
      type: 'create-window',
      windowId: this._id,
      options: this._options,
    });
    this._shown = true;
    this._visible = true;

    if (this._pendingCalls.length) {
      const queued = this._pendingCalls.splice(0);
      for (const { method, args } of queued) {
        this._send(method, ...args);
      }
    }

    return this;
  }

  /**
   * Hide the window without destroying it.
   * @returns {this}
   */
  hide() {
    if (!this._shown || this._destroyed) return this;
    this._send('hide');
    this._visible = false;
    return this;
  }

  _setupIpc() {
    this._ipc.on('message', (msg) => {
      if (msg.windowId !== undefined && msg.windowId !== this._id) return;

      switch (msg.type) {
        case 'event':
          this._handleNativeEvent(msg.event, msg.data || {});
          break;
        case 'close-request':
          this._handleCloseRequest(msg.requestId);
          break;
        default:
          break;
      }
    });

    this._ipc.on('error', (err) => {
      this.emit('error', err);
    });
  }

  _handleNativeEvent(event, data) {
    switch (event) {
      case 'start':
      case 'ready':
        this._ready = true;
        if (this._pendingUrl) {
          const url = this._pendingUrl;
          this._pendingUrl = null;
          this._send('navigate', url);
        }
        this.emit('ready');
        this.emit('start');
        break;
      case 'closed':
        this._destroyed = true;
        const { app } = require('./index');
        app._unregister(this._id);
        this.emit('closed');
        break;
      case 'resize':
        this.emit('resize', data);
        break;
      case 'minimize':
        this._minimized = true;
        this.emit('minimize');
        break;
      case 'maximize':
        this._maximized = true;
        this.emit('maximize');
        break;
      case 'unmaximize':
      case 'restore':
        this._maximized = false;
        this._minimized = false;
        this.emit('restore');
        this.emit('unmaximize');
        break;
      case 'focus':
        this._focused = true;
        this.emit('focus');
        break;
      case 'blur':
        this._focused = false;
        this.emit('blur');
        break;
      case 'move':
        this.emit('move', data);
        break;
      default:
        this.emit(event, data);
    }
  }

  _handleCloseRequest(requestId) {
    let allowed = true;
    const event = {
      preventDefault() {
        allowed = false;
      },
    };
    this.emit('close', event);
    if (this._ipc) {
      this._ipc.send({
        type: 'close-response',
        windowId: this._id,
        requestId,
        allow: allowed,
      });
    }
  }

  /**
   * Load a URL or local file.
   * Safe to call before show() — navigation is queued until the window is ready.
   * @param {string} url
   */
  loadURL(url) {
    if (this._destroyed) return;

    if (!url || typeof url !== 'string') {
      throw new TypeError('loadURL(url) requires a string');
    }

    if (
      !/^https?:\/\//i.test(url) &&
      !url.startsWith('file:') &&
      !url.startsWith('data:') &&
      !url.startsWith('about:')
    ) {
      const abs = path.resolve(url);
      url = 'file:///' + abs.replace(/\\/g, '/');
    }

    if (!this._shown || !this._ipc) {
      this._options.url = url;
      this._pendingUrl = url;
      return;
    }

    if (!this._ready) {
      this._pendingUrl = url;
      return;
    }

    this._send('navigate', url);
  }

  /** Alias for loadURL */
  loadUrl(url) {
    return this.loadURL(url);
  }

  /**
   * Load a local HTML file
   * @param {string} filePath
   */
  loadFile(filePath) {
    const abs = path.resolve(filePath);
    this.loadURL('file:///' + abs.replace(/\\/g, '/'));
  }

  close(options = {}) {
    if (this._destroyed) return;
    if (!this._shown) {
      this._destroyed = true;
      const { app } = require('./index');
      app._unregister(this._id);
      this.emit('closed');
      return;
    }
    this._send('close', options.force === true);
  }

  minimize() { this._send('minimize'); }
  maximize() { this._send('maximize'); }
  restore() { this._send('restore'); }
  focus() { this._send('focus'); }
  blur() { this._send('blur'); }
  flash() { this._send('flash'); }
  center() { this._send('center'); }

  setTitle(title) {
    this._options.title = title;
    this._send('setTitle', title);
  }

  setIcon(iconPathOrDataUrl) {
    this._options.icon = iconPathOrDataUrl;
    this._send('setIcon', iconPathOrDataUrl);
  }

  setSize(width, height) {
    this._options.width = width;
    this._options.height = height;
    this._send('setSize', width, height);
  }

  setMinSize(width, height) {
    this._options.minWidth = width;
    this._options.minHeight = height;
    this._send('setMinSize', width, height);
  }

  setMaxSize(width, height) {
    this._options.maxWidth = width;
    this._options.maxHeight = height;
    this._send('setMaxSize', width, height);
  }

  setPosition(x, y) {
    this._options.x = x;
    this._options.y = y;
    this._options.center = false;
    this._send('setPosition', x, y);
  }

  setBackgroundColor(color) {
    this._options.backgroundColor = color;
    this._send('setBackgroundColor', color);
  }

  isReady() { return this._ready; }
  isDestroyed() { return this._destroyed; }
  isFocused() { return this._focused; }
  isMinimized() { return this._minimized; }
  isMaximized() { return this._maximized; }
  getBounds() { return null; }

  get dialog() {
    const self = this;
    return {
      async info(message, title = 'Information') {
        return self._dialog('info', String(message), String(title));
      },
      async error(message, title = 'Error') {
        return self._dialog('error', String(message), String(title));
      },
      async warning(message, title = 'Warning') {
        return self._dialog('warning', String(message), String(title));
      },
      async confirm(message, title = 'Confirm') {
        const result = await self._dialog('confirm', String(message), String(title));
        if (typeof result === 'boolean') return result;
        return result === true || result === 'true' || result === 'yes' || result === 1;
      },
      async selectFile(options = {}) {
        return self._dialog('selectFile', options || {});
      },
      async selectFolder(options = {}) {
        return self._dialog('selectFolder', options || {});
      },
    };
  }

  _send(method, ...args) {
    if (this._destroyed) return;

    if (!this._shown || !this._ipc) {
      this._pendingCalls.push({ method, args });
      return;
    }

    this._ipc.send({
      type: 'call',
      windowId: this._id,
      method,
      args,
    });
  }

  _dialog(name, ...args) {
    return new Promise((resolve, reject) => {
      if (this._destroyed) {
        reject(new Error('Window is destroyed'));
        return;
      }
      if (!this._shown || !this._ipc) {
        reject(new Error('Call win.show() before using dialogs'));
        return;
      }

      const requestId = `dlg_${Date.now()}_${Math.random().toString(36).slice(2)}`;
      let settled = false;

      const onMessage = (msg) => {
        if (msg.type === 'dialog-response' && msg.requestId === requestId) {
          if (settled) return;
          settled = true;
          this._ipc.off('message', onMessage);
          if (msg.error) reject(new Error(msg.error));
          else resolve(msg.result);
        }
      };

      this._ipc.on('message', onMessage);

      const payload = {
        type: 'dialog',
        windowId: this._id,
        requestId,
        name,
        args,
      };
      if (typeof args[0] === 'string') {
        payload.message = args[0];
        if (typeof args[1] === 'string') payload.title = args[1];
      }

      this._ipc.send(payload);

      setTimeout(() => {
        if (settled) return;
        settled = true;
        this._ipc.off('message', onMessage);
        reject(new Error(`Dialog "${name}" timed out`));
      }, 120000);
    });
  }
}

module.exports = { WebView };
