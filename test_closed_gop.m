function test_closed_gop()
% TEST_CLOSED_GOP Test reading from closed GOP video
%   Throws error on failure. Skips if test file is missing.

if ~isfile('movie_closed_gop.mp4')
    return;  % Skip if file not available
end

vi = open_ffmpeg_video('movie_closed_gop.mp4');
assert(vi.num_frames > 0, 'num_frames should be positive');

% Test reading frame 7075 (previously problematic frame)
frame_idx = min(7075, vi.num_frames);
frame = read_ffmpeg_frame(vi, frame_idx);

assert(size(frame, 1) == vi.height, 'Frame height mismatch');
assert(size(frame, 2) == vi.width, 'Frame width mismatch');

close_ffmpeg_video(vi);

end
