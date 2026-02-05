function timing_test_100(filename, use_videoreader)
% Timing test for 100 random frame accesses
% Usage: timing_test_100('movie.avi', true)   % use VideoReader
%        timing_test_100('movie.mp4', false)  % use FFmpeg

if nargin < 2
    use_videoreader = false;
end

fprintf('Opening %s', filename);
if use_videoreader
    fprintf(' with VideoReader...\n');
    tic;
    vr = VideoReader(filename);
    open_time = toc;
    num_frames = vr.NumFrames;
    width = vr.Width;
    height = vr.Height;
else
    fprintf(' with FFmpeg...\n');
    tic;
    vi = open_ffmpeg_video(filename);
    open_time = toc;
    num_frames = vi.num_frames;
    width = vi.width;
    height = vi.height;
end
fprintf('Opened in %.3f seconds: %d frames, %dx%d\n', open_time, num_frames, width, height);

% Test 100 random frames
rng(42);
num_test = 100;
frame_indices = randperm(num_frames, num_test);

fprintf('\nReading %d random frames...', num_test);
times = zeros(num_test, 1);
for i = 1:num_test
    idx = frame_indices(i);
    tic;
    if use_videoreader
        frame = read(vr, idx);
    else
        frame = read_ffmpeg_frame(vi, idx);
    end
    times(i) = toc * 1000;
end
fprintf(' done.\n\n');

fprintf('Results:\n');
fprintf('  Mean:   %6.1f ms\n', mean(times));
fprintf('  Median: %6.1f ms\n', median(times));
fprintf('  Std:    %6.1f ms\n', std(times));
fprintf('  Min:    %6.1f ms\n', min(times));
fprintf('  Max:    %6.1f ms\n', max(times));
