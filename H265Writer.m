classdef H265Writer < handle
    % H265WRITER Wrapper for H.265 video writing MEX functions
    %   Ensures proper resource cleanup when object is destroyed.
    %
    %   Example (grayscale):
    %       vid = H265Writer('output.mp4', 640, 480, 30);
    %       vid.write(gray_frame);  % height x width uint8
    %       clear vid;
    %
    %   Example (RGB color):
    %       vid = H265Writer('output.mp4', 640, 480, 30, true);
    %       vid.write(rgb_frame);  % height x width x 3 uint8
    %       clear vid;

    properties (SetAccess = private)
        filename
        width
        height
        frame_rate
        is_color
        frames_written = 0
    end

    properties (Access = private)
        writer_info  % struct from open_h265_write
    end

    methods
        function obj = H265Writer(filename, width, height, frame_rate, is_color)
            % H265WRITER Open a video file for writing
            %   vid = H265Writer(filename, width, height, frame_rate)
            %   vid = H265Writer(filename, width, height, frame_rate, is_color)
            %
            %   frame_rate can be a scalar (e.g., 30) or [num, den] (e.g., [30000, 1001])
            %   is_color: false (default) for grayscale, true for RGB color

            if nargin < 5
                is_color = false;
            end

            obj.writer_info = open_h265_write(filename, width, height, frame_rate, is_color);

            obj.filename = filename;
            obj.width = width;
            obj.height = height;
            obj.is_color = is_color;
            if isscalar(frame_rate)
                obj.frame_rate = frame_rate;
            else
                obj.frame_rate = frame_rate(1) / frame_rate(2);
            end
        end

        function write(obj, frame)
            % WRITE Write a frame to the video
            %   vid.write(frame)
            %
            %   For grayscale: frame must be uint8 matrix of size height x width
            %   For RGB color: frame must be uint8 array of size height x width x 3

            if ~isa(frame, 'uint8')
                error('H265Writer:badType', 'Frame must be uint8');
            end

            if obj.is_color
                % RGB mode: expect height x width x 3
                if ndims(frame) ~= 3 || size(frame, 3) ~= 3
                    error('H265Writer:badSize', 'RGB frame must have 3 channels');
                end
                if size(frame, 1) ~= obj.height || size(frame, 2) ~= obj.width
                    error('H265Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
                        size(frame, 1), size(frame, 2), obj.height, obj.width);
                end
            else
                % Grayscale mode: expect height x width
                if ndims(frame) ~= 2
                    error('H265Writer:badSize', 'Grayscale frame must be 2D');
                end
                [h, w] = size(frame);
                if h ~= obj.height || w ~= obj.width
                    error('H265Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
                        h, w, obj.height, obj.width);
                end
            end

            write_h265_frame(obj.writer_info, frame);
            obj.frames_written = obj.frames_written + 1;
        end

        function delete(obj)
            % DELETE Destructor - ensures encoder is flushed and file is closed
            close_h265_write(obj.writer_info);
        end

        function d = duration(obj)
            % DURATION Get current video duration in seconds
            %   d = vid.duration()
            d = obj.frames_written / obj.frame_rate;
        end
    end
end
