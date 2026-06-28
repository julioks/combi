#!/usr/bin/env node

const fs = require("fs");
const path = require("path");

const repoRoot = path.resolve(__dirname, "..");

const defaults = {
  out: path.join(repoRoot, "build", "tape-tests", "tdk-sa-baseline-6000bps.wav"),
  sampleRate: 48000,
  bitRate: 6000,
  amplitude: 0.85,
  width: 20,
  height: 20,
  frames: 120,
  preambleBytes: 24,
  guardBits: 8,
  startSilenceMs: 1000,
  endSilenceMs: 1000,
  convention: "ieee",
  channelLayout: "dual-mono",
};

function parseArgs(argv) {
  const options = { ...defaults };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith("--")) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const key = arg.slice(2);
    const value = argv[++i];
    if (value == null) {
      throw new Error(`Missing value for ${arg}`);
    }
    if (key in options) {
      const numericKeys = new Set([
        "sampleRate",
        "bitRate",
        "amplitude",
        "width",
        "height",
        "frames",
        "preambleBytes",
        "guardBits",
        "startSilenceMs",
        "endSilenceMs",
      ]);
      options[key] = numericKeys.has(key) ? Number(value) : value;
    } else {
      throw new Error(`Unknown option: ${arg}`);
    }
  }
  return options;
}

function assertOptions(options) {
  for (const key of ["sampleRate", "bitRate", "amplitude", "width", "height", "frames"]) {
    if (!Number.isFinite(options[key]) || options[key] <= 0) {
      throw new Error(`${key} must be a positive number`);
    }
  }
  if (options.sampleRate < options.bitRate * 8) {
    throw new Error("sampleRate must be at least 8x bitRate for this generator");
  }
  if (options.width > 255 || options.height > 255) {
    throw new Error("width and height must fit in one protocol byte");
  }
  const pixels = options.width * options.height;
  if (pixels > 2048) {
    throw new Error("pixel count exceeds decoder MAX_GRID_PIXELS default");
  }
  if (!["ieee", "thomas"].includes(options.convention)) {
    throw new Error("convention must be ieee or thomas");
  }
  if (!["dual-mono", "mono", "data-left", "data-right"].includes(options.channelLayout)) {
    throw new Error("channelLayout must be dual-mono, mono, data-left, or data-right");
  }
}

function pushByteBits(bits, value) {
  for (let bit = 7; bit >= 0; bit--) {
    bits.push((value >> bit) & 1);
  }
}

function pushColorBits(bits, color) {
  pushByteBits(bits, color.r);
  pushByteBits(bits, color.g);
  pushByteBits(bits, color.b);
}

function makeFramePixels(frameIndex, width, height) {
  const pixels = [];
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let bit = ((x + frameIndex) & 3) < 2 ? 1 : 0;

      if (y < 8 && x < 8) {
        const bitIndex = y;
        bit = (frameIndex >> bitIndex) & 1;
      }

      if (y === height - 1) {
        bit = (x + frameIndex) & 1;
      }

      pixels.push(bit);
    }
  }
  return pixels;
}

function buildChunkPayloadBits(options, frameIndex) {
  const bits = [];
  pushByteBits(bits, options.width);
  pushByteBits(bits, options.height);
  pushByteBits(bits, 1); // bpp 1, indexed black/white.
  pushColorBits(bits, { r: 0, g: 0, b: 0 });
  pushColorBits(bits, { r: 255, g: 255, b: 255 });
  bits.push(0); // No palette changes for this frame.
  bits.push(...makeFramePixels(frameIndex, options.width, options.height));
  return bits;
}

function buildSignalSegments(options) {
  const segments = [];
  for (let frame = 0; frame < options.frames; frame++) {
    if (frame > 0) {
      segments.push({ type: "guard", halfCount: options.guardBits * 2 });
    }

    const bits = [];
    for (let i = 0; i < options.preambleBytes; i++) {
      pushByteBits(bits, 0x55);
    }
    pushByteBits(bits, 0xd5);
    bits.push(...buildChunkPayloadBits(options, frame));
    segments.push({ type: "bits", bits });
  }
  return segments;
}

function manchesterLevels(bits, convention) {
  const levels = [];
  for (const bit of bits) {
    if (convention === "ieee") {
      levels.push(bit === 0 ? -1 : 1);
      levels.push(bit === 0 ? 1 : -1);
    } else {
      levels.push(bit === 0 ? 1 : -1);
      levels.push(bit === 0 ? -1 : 1);
    }
  }
  return levels;
}

function firstPreambleHalfLevel(convention) {
  return convention === "ieee" ? -1 : 1;
}

function buildHalfLevels(options) {
  const levels = [];
  for (const segment of buildSignalSegments(options)) {
    if (segment.type === "guard") {
      const level = firstPreambleHalfLevel(options.convention);
      for (let i = 0; i < segment.halfCount; i++) {
        levels.push(level);
      }
    } else {
      levels.push(...manchesterLevels(segment.bits, options.convention));
    }
  }
  return levels;
}

function writeAscii(view, offset, value) {
  for (let i = 0; i < value.length; i++) {
    view.setUint8(offset + i, value.charCodeAt(i));
  }
}

function writeWav(samples, options) {
  const channels = options.channelLayout === "mono" ? 1 : 2;
  const bytesPerSample = 2;
  const blockAlign = channels * bytesPerSample;
  const dataSize = samples.length * blockAlign;
  const buffer = Buffer.alloc(44 + dataSize);
  const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);

  writeAscii(view, 0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  writeAscii(view, 8, "WAVE");
  writeAscii(view, 12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, channels, true);
  view.setUint32(24, options.sampleRate, true);
  view.setUint32(28, options.sampleRate * blockAlign, true);
  view.setUint16(32, blockAlign, true);
  view.setUint16(34, 16, true);
  writeAscii(view, 36, "data");
  view.setUint32(40, dataSize, true);

  let offset = 44;
  for (const sample of samples) {
    const clipped = Math.max(-1, Math.min(1, sample));
    const pcm = Math.round(clipped < 0 ? clipped * 0x8000 : clipped * 0x7fff);

    if (channels === 1) {
      view.setInt16(offset, pcm, true);
      offset += 2;
    } else {
      const left = options.channelLayout === "data-right" ? 0 : pcm;
      const right = options.channelLayout === "data-left" ? 0 : pcm;
      view.setInt16(offset, left, true);
      view.setInt16(offset + 2, right, true);
      offset += 4;
    }
  }

  fs.mkdirSync(path.dirname(options.out), { recursive: true });
  fs.writeFileSync(options.out, buffer);
}

function generateSamples(options) {
  const halfLevels = buildHalfLevels(options);
  const halfBitDuration = 1 / (options.bitRate * 2);
  const startSilenceSec = options.startSilenceMs / 1000;
  const endSilenceSec = options.endSilenceMs / 1000;
  const dataDuration = halfLevels.length * halfBitDuration;
  const totalDuration = startSilenceSec + dataDuration + endSilenceSec;
  const totalSamples = Math.ceil(totalDuration * options.sampleRate);
  const samples = new Float32Array(totalSamples);
  const rampSamples = Math.max(
    1,
    Math.round(options.sampleRate * Math.min(halfBitDuration * 0.15, 0.00035))
  );
  const dataStart = startSilenceSec;
  const dataEnd = dataStart + dataDuration;

  for (let i = 0; i < samples.length; i++) {
    const t = i / options.sampleRate;
    if (t < dataStart || t >= dataEnd) {
      samples[i] = 0;
      continue;
    }

    const dataT = t - dataStart;
    const halfIndex = Math.min(halfLevels.length - 1, Math.floor(dataT / halfBitDuration));
    let value = halfLevels[halfIndex] * options.amplitude;

    const sampleInHalf = Math.floor((dataT - halfIndex * halfBitDuration) * options.sampleRate);
    if (sampleInHalf < rampSamples && halfIndex > 0) {
      const previous = halfLevels[halfIndex - 1] * options.amplitude;
      const mix = sampleInHalf / rampSamples;
      value = previous + (value - previous) * mix;
    }

    samples[i] = value;
  }

  return { samples, halfLevels, totalDuration };
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  assertOptions(options);
  const { samples, halfLevels, totalDuration } = generateSamples(options);
  writeWav(samples, options);

  const manifest = {
    ...options,
    out: path.resolve(options.out),
    totalSamples: samples.length,
    totalDurationSeconds: totalDuration,
    signalBitPeriods: halfLevels.length / 2,
    payload: "20x20 bpp1 black/white, one frame per guarded resync chunk",
  };
  const manifestPath = options.out.replace(/\.wav$/i, ".json");
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);

  console.log(`Wrote ${path.resolve(options.out)}`);
  console.log(`Wrote ${path.resolve(manifestPath)}`);
  console.log(`${options.frames} frames, ${options.bitRate} bps, ${totalDuration.toFixed(3)} s`);
}

main();
