function test_gray()
% TEST_GRAY Test reading grayscale encoded video
%   Throws error on failure. Skips if test file is missing.

if ~isfile('movie_gray.mp4')
    return;  % Skip if file not available
end

vi = open_ffmpeg_video('movie_gray.mp4');
assert(vi.num_frames > 0, 'num_frames should be positive');

frame_idx = min(5000, vi.num_frames);
frame = read_ffmpeg_frame(vi, frame_idx);

assert(isa(frame, 'uint8'), 'Frame should be uint8');
assert(size(frame, 1) == vi.height, 'Frame height mismatch');
assert(size(frame, 2) == vi.width, 'Frame width mismatch');

close_ffmpeg_video(vi);

end
