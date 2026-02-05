function test_frame_7075()
% TEST_FRAME_7075 Test reading specific frame that was previously problematic
%   Throws error on failure. Skips if test file is missing.

if ~isfile('movie.mp4')
    return;  % Skip if file not available
end

vi = open_ffmpeg_video('movie.mp4');

if vi.num_frames < 7075
    close_ffmpeg_video(vi);
    return;  % Skip if video too short
end

% Try to read frame 7075
frame = read_ffmpeg_frame(vi, 7075);

assert(size(frame, 1) == vi.height, 'Frame height mismatch');
assert(size(frame, 2) == vi.width, 'Frame width mismatch');

close_ffmpeg_video(vi);

end
