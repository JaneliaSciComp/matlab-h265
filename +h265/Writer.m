classdef Writer < handle
  % WRITER Wrapper for h.265 video writing MEX functions
  %   Ensures proper resource cleanup when object is destroyed.
  %
  %   Example (RGB color, the default):
  %       vid = h265.Writer('output.mp4', 640, 480, 30);
  %       vid.write(rgb_frame);  % height x width x 3 uint8
  %       % vid flushes and closes automatically when it goes out of scope
  %
  %   Example (grayscale):
  %       vid = h265.Writer('output.mp4', 640, 480, 30, 'is_gray', true);
  %       vid.write(gray_frame);  % height x width uint8
  %       % vid flushes and closes automatically when it goes out of scope

  properties (SetAccess = private)
    filename
    width
    height
    frame_rate
    is_gray
    gop_size
    crf
    frames_written = 0
  end

  properties (Access = private)
    writer_info  % struct from open_h265_write
  end

  methods
    function obj = Writer(filename, width, height, frame_rate, varargin)
      % WRITER Open a video file for writing
      %   vid = h265.Writer(filename, width, height, frame_rate)
      %   vid = h265.Writer(filename, width, height, frame_rate, 'is_gray', true)
      %   vid = h265.Writer(filename, width, height, frame_rate, 'crf', 23, 'gop_size', 30)
      %
      %   frame_rate can be a scalar (e.g., 30) or [num, den] (e.g., [30000, 1001])
      %
      %   Optional parameters:
      %     is_gray  - boolean (default false): false for RGB color, true for grayscale
      %     gop_size - keyframe interval in frames (default 50)
      %     crf      - quality setting, 0-51 where lower is better quality (default 18)

      [is_gray, gop_size, crf] = myparse(varargin, ...
        'is_gray', false, 'gop_size', 50, 'crf', 18);

      is_color = ~is_gray;
      obj.writer_info = h265.open_h265_write(filename, width, height, frame_rate, ...
        is_color, gop_size, crf);

      obj.filename = filename;
      obj.width = width;
      obj.height = height;
      obj.is_gray = is_gray;
      obj.gop_size = gop_size;
      obj.crf = crf;
      if isscalar(frame_rate)
        obj.frame_rate = frame_rate;
      else
        obj.frame_rate = frame_rate(1) / frame_rate(2);
      end
    end

    function write(obj, frames)
      % WRITE Write one or more frames to the video
      %   vid.write(frame)
      %   vid.write(frames)
      %
      %   For grayscale: frames must be uint8 array of size height x width x num_frames
      %                  (single frame can be height x width)
      %   For RGB color: frames must be uint8 array of size height x width x 3 x num_frames
      %                  (single frame can be height x width x 3)

      if ~isa(frames, 'uint8')
        error('Writer:badType', 'Frames must be uint8');
      end

      nd = ndims(frames);

      if obj.is_gray
        % Grayscale mode: expect height x width x num_frames (or height x width for single)
        if nd == 2
          % Single frame: height x width
          if size(frames, 1) ~= obj.height || size(frames, 2) ~= obj.width
            error('Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
              size(frames, 1), size(frames, 2), obj.height, obj.width);
          end
          num_frames = 1;
        elseif nd == 3
          % Multiple frames: height x width x num_frames
          if size(frames, 1) ~= obj.height || size(frames, 2) ~= obj.width
            error('Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
              size(frames, 1), size(frames, 2), obj.height, obj.width);
          end
          num_frames = size(frames, 3);
        else
          error('Writer:badSize', 'Grayscale frames must be 2D (single) or 3D (batch)');
        end
      else
        % RGB mode: expect height x width x 3 x num_frames (or height x width x 3 for single)
        if nd == 3
          % Single frame: height x width x 3
          if size(frames, 3) ~= 3
            error('Writer:badSize', 'RGB frame must have 3 channels');
          end
          if size(frames, 1) ~= obj.height || size(frames, 2) ~= obj.width
            error('Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
              size(frames, 1), size(frames, 2), obj.height, obj.width);
          end
          num_frames = 1;
        elseif nd == 4
          % Multiple frames: height x width x 3 x num_frames
          if size(frames, 3) ~= 3
            error('Writer:badSize', 'RGB frames must have 3 channels');
          end
          if size(frames, 1) ~= obj.height || size(frames, 2) ~= obj.width
            error('Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
              size(frames, 1), size(frames, 2), obj.height, obj.width);
          end
          num_frames = size(frames, 4);
        else
          error('Writer:badSize', 'RGB frames must be 3D (single) or 4D (batch)');
        end
      end

      h265.write_h265_frames(obj.writer_info, frames);
      obj.frames_written = obj.frames_written + num_frames;
    end

    function delete(obj)
      % DELETE Destructor - ensures encoder is flushed and file is closed
      h265.close_h265_write(obj.writer_info);
    end

    function d = duration(obj)
      % DURATION Get current video duration in seconds
      %   d = vid.duration()
      d = obj.frames_written / obj.frame_rate;
    end
  end
end
