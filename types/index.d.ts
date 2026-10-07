import { EventEmitter } from 'events';

export interface WebViewOptions {
  url?: string;
  title?: string;
  width?: number;
  height?: number;
  x?: number;
  y?: number;
  minWidth?: number;
  minHeight?: number;
  maxWidth?: number;
  maxHeight?: number;
  icon?: string;
  resizable?: boolean;
  maximizable?: boolean;
  minimizable?: boolean;
  closable?: boolean;
  center?: boolean;
  frame?: boolean;
  transparent?: boolean;
  alwaysOnTop?: boolean;
  fullscreen?: boolean;
  backgroundColor?: string;
  devTools?: boolean;
  windowsHide?: boolean;
  userDataFolder?: string;
}

export interface CloseEvent {
  preventDefault(): void;
}

export interface SizeData {
  width: number;
  height: number;
}

export interface PositionData {
  x: number;
  y: number;
}

export interface DialogOptions {
  title?: string;
  filters?: Array<{ name: string; extensions: string[] }>;
  multiple?: boolean;
  defaultPath?: string;
}

export interface WebViewDialog {
  info(message: string, title?: string): Promise<void>;
  error(message: string, title?: string): Promise<void>;
  warning(message: string, title?: string): Promise<void>;
  confirm(message: string, title?: string): Promise<boolean>;
  selectFile(options?: DialogOptions): Promise<string | string[] | null>;
  selectFolder(options?: DialogOptions): Promise<string | null>;
}

export declare class WebView extends EventEmitter {
  constructor(options?: WebViewOptions);

  show(): this;
  hide(): this;
  loadURL(url: string): void;
  loadUrl(url: string): void;
  loadFile(filePath: string): void;

  close(options?: { force?: boolean }): void;
  minimize(): void;
  maximize(): void;
  restore(): void;
  focus(): void;
  blur(): void;
  flash(): void;
  center(): void;

  setTitle(title: string): void;
  setIcon(iconPathOrDataUrl: string): void;
  setSize(width: number, height: number): void;
  setMinSize(width: number, height: number): void;
  setMaxSize(width: number, height: number): void;
  setPosition(x: number, y: number): void;
  setBackgroundColor(color: string): void;

  isReady(): boolean;
  isDestroyed(): boolean;
  isFocused(): boolean;
  isMinimized(): boolean;
  isMaximized(): boolean;
  getBounds(): { x: number; y: number; width: number; height: number } | null;

  readonly dialog: WebViewDialog;

  on(event: 'ready' | 'start', listener: () => void): this;
  on(event: 'close', listener: (event: CloseEvent) => void): this;
  on(event: 'closed', listener: () => void): this;
  on(event: 'resize', listener: (data: SizeData) => void): this;
  on(event: 'move', listener: (data: PositionData) => void): this;
  on(event: 'minimize' | 'maximize' | 'unmaximize' | 'restore' | 'focus' | 'blur', listener: () => void): this;
  on(event: 'error', listener: (err: Error) => void): this;
  on(event: string, listener: (...args: any[]) => void): this;

  once(event: 'ready' | 'start', listener: () => void): this;
  once(event: 'close', listener: (event: CloseEvent) => void): this;
  once(event: 'closed', listener: () => void): this;
  once(event: string, listener: (...args: any[]) => void): this;
}

export interface WebViewApp extends EventEmitter {
  getAllWindows(): WebView[];
  getFocusedWindow(): WebView | null;
  closeAll(options?: { force?: boolean }): void;

  on(event: 'all-closed', listener: () => void): this;
  once(event: 'all-closed', listener: () => void): this;
}

export declare const app: WebViewApp;
