classdef H265Writer < handle
    % H265WRITER Wrapper for H.265 video writing MEX functions
    %   Ensures proper resource cleanup when object is destroyed.
    %
    %   Example:
    %       vid = H265Writer('output.mp4', 640, 480, 30);
    %       vid.write(frame1);
    %       vid.write(frame2);
    %       clear vid;  % automatically closes and flushes

    properties (SetAccess = private)
        filename
        width
        height
        frame_rate
        frames_written = 0
    end

    properties (Access = private)
        writer_info  % struct from open_h265_write
    end

    methods
        function obj = H265Writer(filename, width, height, frame_rate)
            % H265WRITER Open a video file for writing
            %   vid = H265Writer(filename, width, height, frame_rate)
            %
            %   frame_rate can be a scalar (e.g., 30) or [num, den] (e.g., [30000, 1001])

            obj.writer_info = open_h265_write(filename, width, height, frame_rate);

            obj.filename = filename;
            obj.width = width;
            obj.height = height;
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
            %   frame must be a uint8 matrix of size height x width

            if ~isa(frame, 'uint8')
                error('H265Writer:badType', 'Frame must be uint8');
            end

            [h, w] = size(frame);
            if h ~= obj.height || w ~= obj.width
                error('H265Writer:badSize', 'Frame size %dx%d does not match video %dx%d', ...
                    h, w, obj.height, obj.width);
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
