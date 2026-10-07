'use strict';

const { EventEmitter } = require('events');
const { spawn } = require('child_process');
const path = require('path');
const net = require('net');
const fs = require('fs');

/**
 * Simple length-prefixed JSON IPC client.
 *
 * Protocol:
 *   [4-byte little-endian length][utf-8 json payload]
 *
 * By default it starts the native host executable that should sit
 * next to the package or be configured via WEBVIEW2_HOST env var.
 */
class IpcClient extends EventEmitter {
  constructor(options = {}) {
    super();
    this._socket = null;
    this._buffer = Buffer.alloc(0);
    this._started = false;

    this._startHost(options);
  }

  _getHostPath() {
    // 1. Environment variable override
    if (process.env.WEBVIEW_HOST || process.env.WEBVIEW2_HOST) {
      return process.env.WEBVIEW_HOST || process.env.WEBVIEW2_HOST;
    }

    // 2. Look next to the package / in common locations
    const candidates = [
      path.join(__dirname, '..', 'native', 'webview2-host.exe'),
      path.join(__dirname, '..', 'bin', 'webview2-host.exe'),
      path.join(__dirname, '..', 'webview2-host.exe'),
      path.join(process.cwd(), 'webview2-host.exe'),
      path.join(process.cwd(), 'bin', 'webview2-host.exe'),
    ];

    for (const p of candidates) {
      if (fs.existsSync(p)) return p;
    }

    return null;
  }

  _startHost(options = {}) {
    const hostPath = this._getHostPath();

    if (!hostPath) {
      // For development we still allow the module to load.
      // Real calls will fail with a clear error until the host exists.
      this.emit('error', new Error(
        'Native host (webview2-host.exe) not found.\n' +
        'Set WEBVIEW2_HOST environment variable or place the executable next to the package.'
      ));
      return;
    }

    // Start the native process. We communicate over stdin/stdout for simplicity.
    // (Named pipes can be added later for higher performance.)
    this._proc = spawn(hostPath, [], {
      stdio: ['pipe', 'pipe', 'inherit'],
      windowsHide: options.windowsHide === true,
    });

    this._proc.on('error', (err) => {
      this.emit('error', err);
    });

    this._proc.on('exit', (code) => {
      this.emit('exit', code);
    });

    this._proc.stdout.on('data', (chunk) => {
      this._buffer = Buffer.concat([this._buffer, chunk]);
      this._processBuffer();
    });

    this._started = true;
  }

  _processBuffer() {
    while (this._buffer.length >= 4) {
      const len = this._buffer.readUInt32LE(0);
      if (this._buffer.length < 4 + len) break;

      const payload = this._buffer.slice(4, 4 + len).toString('utf8');
      this._buffer = this._buffer.slice(4 + len);

      try {
        const msg = JSON.parse(payload);
        this.emit('message', msg);
      } catch (e) {
        this.emit('error', new Error('Invalid JSON from native host: ' + e.message));
      }
    }
  }

  /**
   * Send a message to the native host
   * @param {object} msg
   */
  send(msg) {
    if (!this._proc || !this._proc.stdin.writable) {
      this.emit('error', new Error('Native host is not running'));
      return;
    }

    const json = JSON.stringify(msg);
    const body = Buffer.from(json, 'utf8');
    const header = Buffer.alloc(4);
    header.writeUInt32LE(body.length, 0);

    this._proc.stdin.write(Buffer.concat([header, body]));
  }

  destroy() {
    if (this._proc) {
      this._proc.kill();
      this._proc = null;
    }
  }
}

module.exports = { IpcClient, getSharedIpc };

let _shared = null;
function getSharedIpc(options = {}) {
  if (!_shared) _shared = new IpcClient(options);
  return _shared;
}
