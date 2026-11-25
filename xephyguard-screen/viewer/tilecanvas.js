/**
 * TileCanvas manages rendering decoded tiles to an HTML5 canvas.
 * Fixed constants: Tile size is 64x64.
 */
export const TILE_SIZE = 64;

export class TileCanvas {
  /**
   * @param {HTMLCanvasElement} canvas
   */
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = (canvas && typeof canvas.getContext === 'function') ? canvas.getContext('2d') : null;
    this.width = 0;
    this.height = 0;
    this.pendingTiles = [];
    this.rafId = null;
    this.banner = null; // 'MASKED' | 'SECURE_DESKTOP' | null
    this.cursorEl = null; // pointer marker (DOM overlay, so it never becomes part of the picture)
    this.cursorX = -1;
    this.cursorY = -1;
  }

  /**
   * Shows the remote pointer at (x, y) in remote pixels (screen Step 34). A small DOM marker is positioned over
   * the canvas (its parent should be position: relative); no-op without a DOM. Hidden while a banner is shown.
   */
  setCursor(x, y) {
    this.cursorX = x;
    this.cursorY = y;
    this.updateCursor();
  }

  updateCursor() {
    const c = this.canvas;
    if (!c || !c.parentElement || typeof document === 'undefined' || this.width <= 0 || this.height <= 0) return;
    if (!this.cursorEl) {
      const el = document.createElement('div');
      el.style.cssText = 'position:absolute;width:10px;height:10px;margin:-5px 0 0 -5px;border-radius:50%;' +
        'background:#fff;border:2px solid #d00;box-sizing:border-box;pointer-events:none;z-index:2';
      c.parentElement.appendChild(el);
      this.cursorEl = el;
    }
    const visible = this.cursorX >= 0 && !this.banner;
    this.cursorEl.style.display = visible ? 'block' : 'none';
    if (!visible) return;
    this.cursorEl.style.left = (c.offsetLeft + (this.cursorX * c.clientWidth) / this.width) + 'px';
    this.cursorEl.style.top = (c.offsetTop + (this.cursorY * c.clientHeight) / this.height) + 'px';
  }

  /**
   * Resizes the canvas if dimensions have changed.
   * @param {number} w
   * @param {number} h
   */
  setSize(w, h) {
    if (this.width !== w || this.height !== h) {
      this.width = w;
      this.height = h;
      if (this.canvas) {
        this.canvas.width = w;
        this.canvas.height = h;
      }
    }
  }

  /**
   * Set status banner text to display over canvas.
   * @param {string|null} bannerText
   */
  setBanner(bannerText) {
    this.banner = bannerText;
    if (this.cursorEl) this.updateCursor();
    this.scheduleRender();
  }

  /**
   * Queue a batch of decoded tiles to be drawn on the next animation frame.
   * @param {Array<{x: number, y: number, bitmap: ImageBitmap}>} x, y are pixels tiles
   */
  drawTiles(tiles) {
    if (!tiles || tiles.length === 0) return;
    this.pendingTiles.push(...tiles);
    this.scheduleRender();
  }

  scheduleRender() {
    if (this.rafId !== null) return;
    if (typeof requestAnimationFrame === 'function') {
      this.rafId = requestAnimationFrame(() => this.flush());
    } else {
      // Node.js or environment without requestAnimationFrame
      this.flush();
    }
  }

  flush() {
    this.rafId = null;
    if (!this.ctx) return;

    // Draw all pending decoded bitmaps in one batch
    while (this.pendingTiles.length > 0) {
      const tile = this.pendingTiles.shift();
      if (tile && tile.bitmap) {
        this.ctx.drawImage(tile.bitmap, tile.x, tile.y);
        if (typeof tile.bitmap.close === 'function') {
          tile.bitmap.close();
        }
      }
    }

    // Overlay banner if set
    if (this.banner) {
      this.ctx.save();
      this.ctx.fillStyle = 'rgba(0, 0, 0, 0.75)';
      this.ctx.fillRect(0, 0, this.width, this.height);

      this.ctx.fillStyle = '#FFFFFF';
      this.ctx.font = 'bold 24px sans-serif';
      this.ctx.textAlign = 'center';
      this.ctx.textBaseline = 'middle';
      this.ctx.fillText(this.banner, this.width / 2, this.height / 2);
      this.ctx.restore();
    }
  }

  clear() {
    this.pendingTiles = [];
    this.cursorX = this.cursorY = -1;
    if (this.cursorEl) this.cursorEl.style.display = 'none';
    if (this.ctx && this.width > 0 && this.height > 0) {
      this.ctx.clearRect(0, 0, this.width, this.height);
    }
  }
}
