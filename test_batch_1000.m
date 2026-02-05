function test_batch_1000()
% TEST_BATCH_1000 Test batch reading of 1000 frames
%   Throws error on failure. Skips if test file is missing.

if ~isfile('movie_keyint10.mp4')
    return;  % Skip if file not available
end

vi = open_ffmpeg_video('movie_keyint10.mp4');

if vi.num_frames < 6000
    close_ffmpeg_video(vi);
    return;  % Skip if video too short
end

% Read 1000 frames in batch
frames = read_ffmpeg_frames(vi, 5000, 5999);

assert(size(frames, 3) == 1000, 'Expected 1000 frames, got %d', size(frames, 3));
assert(size(frames, 1) == vi.height, 'Height mismatch');
assert(size(frames, 2) == vi.width, 'Width mismatch');

close_ffmpeg_video(vi);

end
