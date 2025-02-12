# Vapoursynth rowmanip Plugin

Vapoursynth port of Avisynth built-in function SeparateRows and WeaveRows.

## Usage

```python

core.rowmanip.SeparateRows(clip src, int interval) # SeparateRows separates the rows of each frame into interval frames. The number of frames of the new clip is interval times the number of frames of the old clip. The height of the frame must be a multiple of interval, otherwise an error is thrown. 
core.rowmanip.WeaveRows(clip src, int period) # WeaveRows is the opposite of SeparateRows: it weaves the rows of period frames into a single output frame. The number of frames of the new clip is the ceiling of 'the number of frames of the input clip divided by period'.

```

Supports 8-16bit int or 32bit float input.

## Building

```bash
meson build
ninja -C build install
```