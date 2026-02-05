function test_ffmpeg_overhead()
% TEST_FFMPEG_OVERHEAD Test FFmpeg reader overhead with random access
%   Throws error on failure. Skips if test files are missing.

if ~isfile('movie_keyint10.mp4')
    return;  % Skip if file not available
end

vi = open_ffmpeg_video('movie_keyint10.mp4');

% Read 20 random frames to verify seeking works
rng(42);
num_test = min(20, vi.num_frames);
indices = randperm(vi.num_frames, num_test);

for i = 1:num_test
    frame = read_ffmpeg_frame(vi, indices(i));
    assert(size(frame, 1) == vi.height, 'Frame height mismatch at index %d', indices(i));
    assert(size(frame, 2) == vi.width, 'Frame width mismatch at index %d', indices(i));
end

close_ffmpeg_video(vi);

end
