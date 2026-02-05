function test_gray_speed()
% TEST_GRAY_SPEED Test batch reading from grayscale video
%   Throws error on failure. Skips if test file is missing.

if ~isfile('movie_gray.mp4')
    return;  % Skip if file not available
end

vi_gray = open_ffmpeg_video('movie_gray.mp4');

if vi_gray.num_frames < 6000
    close_ffmpeg_video(vi_gray);
    return;  % Skip if video too short
end

% Read 1000 frames in batch
frames = read_ffmpeg_frames(vi_gray, 5000, 5999);

assert(size(frames, 3) == 1000, 'Expected 1000 frames');
assert(size(frames, 1) == vi_gray.height, 'Height mismatch');
assert(size(frames, 2) == vi_gray.width, 'Width mismatch');

close_ffmpeg_video(vi_gray);

end
