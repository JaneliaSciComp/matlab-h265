function timing_test_videoreader(filename)
% Timing test for random frame access using VideoReader
% Usage: timing_test_videoreader('movie.avi')

fprintf('Opening %s with VideoReader...\n', filename);
tic;
vr = VideoReader(filename);
open_time = toc;
num_frames = vr.NumFrames;
fprintf('Opened in %.3f seconds: %d frames, %dx%d\n', open_time, num_frames, vr.Width, vr.Height);

% Test 10 random frames
rng(42);
num_test = 10;
frame_indices = randperm(num_frames, num_test);

fprintf('\nReading %d random frames:\n', num_test);
fprintf('%8s  %8s\n', 'Frame', 'Time(ms)');
fprintf('%8s  %8s\n', '-----', '--------');

times = zeros(num_test, 1);
for i = 1:num_test
    idx = frame_indices(i);
    tic;
    frame = read(vr, idx);
    times(i) = toc * 1000;
    fprintf('%8d  %8.1f\n', idx, times(i));
end

fprintf('%8s  %8s\n', '-----', '--------');
fprintf('%8s  %8.1f (mean)\n', '', mean(times));
fprintf('%8s  %8.1f (median)\n', '', median(times));
fprintf('%8s  %8.1f (max)\n', '', max(times));
