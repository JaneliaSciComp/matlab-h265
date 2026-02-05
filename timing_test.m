function timing_test(filename)
% Timing test for random frame access
% Usage: timing_test('movie_keyint50.mp4')

fprintf('Opening %s...\n', filename);
tic;
vi = open_ffmpeg_video(filename);
open_time = toc;
fprintf('Opened in %.3f seconds: %d frames, %dx%d\n', open_time, vi.num_frames, vi.width, vi.height);

% Test 10 random frames
rng(42);
num_test = 10;
frame_indices = randperm(vi.num_frames, num_test);

fprintf('\nReading %d random frames:\n', num_test);
fprintf('%8s  %8s\n', 'Frame', 'Time(ms)');
fprintf('%8s  %8s\n', '-----', '--------');

times = zeros(num_test, 1);
for i = 1:num_test
    idx = frame_indices(i);
    tic;
    frame = read_ffmpeg_frame(vi, idx);
    times(i) = toc * 1000;
    fprintf('%8d  %8.1f\n', idx, times(i));
end

fprintf('%8s  %8s\n', '-----', '--------');
fprintf('%8s  %8.1f (mean)\n', '', mean(times));
fprintf('%8s  %8.1f (median)\n', '', median(times));
fprintf('%8s  %8.1f (max)\n', '', max(times));
