classdef H265Reader < handle
    % FFMPEGREADER Wrapper for FFmpeg video reading MEX functions
    %   Ensures proper resource cleanup when object is destroyed.
    %
    %   Example:
    %       vid = H265Reader('movie.mp4');
    %       frame = vid.read(1);
    %       frames = vid.read(1, 100);  % batch read
    %       clear vid;  % automatically closes
    %
    %       % For grayscale video (returns 2D frames instead of RGB):
    %       vid = H265Reader('gray_movie.mp4', true);

    properties (SetAccess = private)
        filename
        num_frames
        width
        height
        frame_rate_num
        frame_rate_den
        time_base_num
        time_base_den
        pts_increment
        is_gray  % true to return grayscale frames (2D), false for RGB (3D)
    end

    properties (Dependent)
        frame_rate
    end

    properties (Access = private)
        video_info  % struct from open_h265_video
    end

    methods
        function obj = H265Reader(filename, is_gray)
            % FFMPEGREADER Open a video file for reading
            %   vid = H265Reader(filename)
            %   vid = H265Reader(filename, is_gray)
            %
            %   is_gray - optional boolean (default false): if true, return
            %             grayscale frames (height x width) instead of RGB
            %             (height x width x 3)

            if nargin < 2
                is_gray = false;
            end

            obj.video_info = open_h265_video(filename);

            % Add is_gray to video_info for MEX functions
            obj.video_info.is_gray = is_gray;
            obj.is_gray = is_gray;

            % Copy properties for easy access
            obj.filename = obj.video_info.filename;
            obj.num_frames = obj.video_info.num_frames;
            obj.width = obj.video_info.width;
            obj.height = obj.video_info.height;
            obj.frame_rate_num = obj.video_info.frame_rate_num;
            obj.frame_rate_den = obj.video_info.frame_rate_den;
            obj.time_base_num = obj.video_info.time_base_num;
            obj.time_base_den = obj.video_info.time_base_den;
            obj.pts_increment = obj.video_info.pts_increment;
        end

        function frame = read(obj, start_frame, end_frame)
            % READ Read one or more frames from the video
            %   frame = vid.read(frame_idx)           - read single frame
            %   frames = vid.read(start_idx, end_idx) - read range of frames
            %
            %   Frame indices are 1-based.

            if nargin < 3
                % Single frame
                frame = read_h265_frame(obj.video_info, start_frame);
            else
                % Batch read
                frame = read_h265_frames(obj.video_info, start_frame, end_frame);
            end
        end

        function delete(obj)
            % DELETE Destructor - ensures resources are freed
            close_h265_video(obj.video_info);
        end

        function fr = get.frame_rate(obj)
            % GET.FRAME_RATE Get frame rate as a double
            fr = double(obj.frame_rate_num) / double(obj.frame_rate_den);
        end

        function d = duration(obj)
            % DURATION Get video duration in seconds
            %   d = vid.duration()
            d = obj.num_frames / obj.frame_rate;
        end
    end
end
