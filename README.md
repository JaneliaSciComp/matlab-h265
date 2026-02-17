# matlab-h265

A MATLAB library for reading and writing h.265 (HEVC) video files,
designed for machine vision applications. Provides cross-platform
h.265 support with no dependence on GStreamer or other system video
frameworks (besides FFmpeg, that is). (Ummm, at least in principle.  So far
has only been tested on Ubuntu 24.04.)  A key goal is for random
access to individual frames (addressed by frame index) to be 100%
reliable and repeatable; for this reason we use only software encoding
and decoding.

## Requirements

- MATLAB R2019b or later
- FFmpeg development libraries: libavformat, libavcodec, libavutil,
  libswscale

## Installation

1. Clone the repository

2. Add the repository root to your MATLAB path:
   ```matlab
   addpath('/path/to/matlab-h265');
   modpath();
   ```
   
3. Build the MEX files:
   ```matlab
   h265.build();
   ```

## Usage

### Writing video

```matlab
% RGB video (default)
writer = h265.Writer('output.mp4', 640, 480, 30);
writer.write(rgb_frame);  % height x width x 3 uint8
% writer closes automatically when it goes out of scope

% Grayscale video
writer = h265.Writer('output.mp4', 640, 480, 30, 'is_gray', true);
writer.write(gray_frame);  % height x width uint8
```

### Reading video

```matlab
reader = h265.Reader('movie.mp4');
frame = reader.read(1);            % read single frame
frames = reader.read(1, 100);      % read frames 1-100
% reader closes automatically when it goes out of scope
```

**Note:** The Reader only supports h.265 files encoded with closed GOPs.
Videos created with `h265.Writer` use closed GOPs, but arbitrary h.265
files from other sources will likely not work.

### Converting from UFMF

```matlab
h265.from_ufmf('output.mp4', 'input.ufmf');
```

## License

BSD 3-Clause License. See [LICENSE](LICENSE) for details.

The UFMF utility functions in `toolbox/` are taken from the
[JAABA project](https://github.com/kristinbranson/JAABA) and carry their
own copyright and license.
