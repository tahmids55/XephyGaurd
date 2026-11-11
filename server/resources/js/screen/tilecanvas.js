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
    this.scheduleRender();
  }

  /**
   * Queue a batch of decoded tiles to be drawn on the next animation frame.
   * @param {Array<{tileX: number, tileY: number, bitmap: ImageBitmap}>} tiles
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
        const dx = tile.tileX * TILE_SIZE;
        const dy = tile.tileY * TILE_SIZE;
        this.ctx.drawImage(tile.bitmap, dx, dy);
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
    if (this.ctx && this.width > 0 && this.height > 0) {
      this.ctx.clearRect(0, 0, this.width, this.height);
    }
  }
}
