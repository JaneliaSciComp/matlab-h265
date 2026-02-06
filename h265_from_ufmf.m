function h265_from_ufmf(h265_file, ufmf_file, varargin)
% H265_FROM_UFMF Convert a UFMF file to H.265 video
%
%   h265_from_ufmf(h265_file, ufmf_file)
%   h265_from_ufmf(h265_file, ufmf_file, 'block_size', 1000)
%
%   Reads frames from a UFMF file in blocks and writes them to an H.265
%   encoded MP4 file.
%
%   Arguments:
%     h265_file  - path to output .mp4 file (will be overwritten if exists)
%     ufmf_file  - path to input .ufmf file
%
%   Optional parameters:
%     block_size  - number of frames to read/write at once (default: 1000)
%     frame_rate  - output frame rate in fps (default: computed from timestamps)
%     frame_count - number of frames to convert (default: all frames)
%
%   Example:
%     h265_from_ufmf('movie.mp4', 'movie.ufmf');
%     h265_from_ufmf('movie.mp4', 'movie.ufmf', 'block_size', 500);
%     h265_from_ufmf('movie.mp4', 'movie.ufmf', 'frame_rate', 30);
%     h265_from_ufmf('movie.mp4', 'movie.ufmf', 'frame_count', 10000);

[block_size, frame_rate, frame_count] = myparse(varargin, ...
    'block_size', 1000, 'frame_rate', [], 'frame_count', []);

% Read UFMF header
header = ufmf_read_header(ufmf_file);
header_cleanup = onCleanup(@() fclose(header.fid));

% Get video properties
height = header.nr;
width = header.nc;
is_color = header.ncolors == 3;

% Determine frame rate
if isempty(frame_rate)
    if header.nframes > 1
        timestamps = header.timestamps;
        avg_dt = (timestamps(end) - timestamps(1)) / (header.nframes - 1);
        frame_rate = 1 / avg_dt;
    else
        error('h265_from_ufmf:noFrameRate', ...
            'Cannot compute frame rate from single-frame video. Specify ''frame_rate'' manually.');
    end
end

% Determine number of frames to convert
if isempty(frame_count)
    num_frames = header.nframes;
else
    num_frames = min(frame_count, header.nframes);
end

% Create H.265 writer
writer = H265Writer(h265_file, width, height, frame_rate, 'is_gray', ~is_color);

% Process frames in blocks
num_blocks = ceil(num_frames / block_size);
frames_written = 0;

for block = 1:num_blocks
    % Compute frame range for this block
    start_frame = (block - 1) * block_size + 1;
    end_frame = min(block * block_size, num_frames);
    frames_in_block = end_frame - start_frame + 1;

    % Pre-allocate block array
    if is_color
        block_data = zeros(height, width, 3, frames_in_block, 'uint8');
    else
        block_data = zeros(height, width, frames_in_block, 'uint8');
    end

    % Read frames in this block
    for i = 1:frames_in_block
        frame_idx = start_frame + i - 1;
        im = ufmf_read_frame(header, frame_idx);

        if is_color
            block_data(:, :, :, i) = im;
        else
            block_data(:, :, i) = im;
        end
    end

    % Write block to H.265
    writer.write(block_data);

    frames_written = frames_written + frames_in_block;
    fprintf('Converted %d/%d frames (%.1f%%)\n', frames_written, num_frames, ...
        100 * frames_written / num_frames);
end

% Cleanup: writer closes automatically, header_cleanup closes file
clear writer header_cleanup;

fprintf('Done. Output: %s\n', h265_file);
end
