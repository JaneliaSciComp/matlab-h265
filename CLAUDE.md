# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
This is a library for reading and writing h.265 video files, geared towards the needs of scientists
using machine vision tools on such videos.

## Development Setup
### Matlab Setup
When starting a new Matlab session, this command should always be run first, to set up the Matlab path:
```matlab
modpath();  % Sets up Matlab path
```

### Building MEX Files
From MATLAB:
```matlab
h265.build()          % Build out-of-date MEX files
h265.build('clean')   % Delete all MEX files
h265.build('rebuild') % Clean and rebuild all
```

Or from the command line:
```bash
cd +h265 && make
```

Requires FFmpeg development libraries: libavformat, libavcodec, libavutil, libswscale.

### Running Tests
```matlab
h265.test_all()        % Run all tests
```

## Architecture

### MATLAB Package (h265)
All H.265 functionality is in the `h265` package (`+h265/` folder):

**Classes (High-Level API)**
- **h265.Reader**: Read H.265 video files. Supports single frame or batch reads. Optional `is_gray` flag for grayscale output.
- **h265.Writer**: Write H.265 video files. Supports single frame or batch writes. Defaults to RGB; pass `is_gray=true` for grayscale.

**MEX Functions (Low-Level C API)**
Reading: `open_h265_video.c` → `read_h265_frame.c` / `read_h265_frames.c` → `close_h265_video.c`

Writing: `open_h265_write.c` → `write_h265_frames.c` → `close_h265_write.c`

MEX functions pass FFmpeg context pointers between calls via a MATLAB struct.

### Key Implementation Details
- H.265 encoding uses YUV420P pixel format with CRF 18 quality
- Closed GOP with keyframe interval of 50 frames
- Frame data: MATLAB column-major; MEX functions handle transpose to/from row-major
- Grayscale frames: height × width (single) or height × width × num_frames (batch)
- RGB frames: height × width × 3 (single) or height × width × 3 × num_frames (batch)

## Key Directories
- `+h265/`: MATLAB package containing Reader, Writer classes, MEX functions, Makefile, and tests
- `toolbox/`: Contains general utilities and functions for reading/writing .ufmf files

## Important Files
- `modpath.m`: Path setup utility
- `+h265/from_ufmf.m`: Converts UFMF files to H.265, processing frames in configurable blocks

## Matlab coding conventions
- Indents should all be two spaces.  Top-level functions should not be indented.  Never use tabs.
- Local variables in functions/methods should only be overwritten if necessary for performance.  Prefer to create new variables holding evolving versions of some value.
- Prefer explicit variable names, even if they are long; and avoid abbreviations.  Use a shorter English word that means the same thing instead of an abbreviation.
- Use spaces liberally in long expressions to add clarity.  E.g. add a space after each comma in the argument list for functions.
- But don't insert a space before the final semicolon in a line of code.
- Use myparse() to handle optional arguments, except in mex files.
- The "end" keyword at the end of a function should be followed by the comment "% function".  Same for end of a methods block and a classdef block.
- Individual lines should not be longer than 160 characters.
- switch statements that check for multiple enumerated cases should enumerate all the handled cases explicitly, and throw an error in the "otherwise:" clause.  This makes it easier to find inappropriately-handled cases when testing.
- When creating a resource that needs later cleanup, *always* use an `onCleanup()` call immediately after acquiring the resource.  But note that this is not
  needed when creating objects like VideoWriter, since they have `delete()` methods that will be called when they go out of scope.
- Avoid the use of `clear` in functions.  Use the `delete()` method for objects or set the variable to `[]`.
- Test functions (besides `test_all()`) should take zero arguments and return zero arguments, but must error if something fails.  Sometimes this will require setting a somewhat arbitrary threshold for what counts as failure.
- Test functions (besides `test_all()`) should not produce a lot of fprintf() output.  Success or failure is the main thing.
- Logical variables should start with some conjugation of "to be" or "to do".  E.g. `is_gray`, `does_need_flush`.
- Index variables should end in `_index`
- Variable that are the count of something should end in `_count`, with a singular stem.  E.g.  `frame_count` instead of `num_frames` or `frames_count`.
- When running `matlab -batch <command>`, there's no need to add the `-nodisplay` option, as it is implied by `-batch`
- When running `matlab -batch <command>`, the <command> cannot contain newlines.  To do something longer, write a .m file and call it in <command>.
- When calling zero-argument functions, include the parentheses for clarity.  E.g. `foo = tempname();`, not `foo = tempname;`
- When a variable holds a file name, the name should end in `_file_name`, not `_file`, for clarity.