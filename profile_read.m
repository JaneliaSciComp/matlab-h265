% Profile where time is spent in read operations

vi = open_ffmpeg_video('movie_keyint10.mp4');
fprintf('Opened video: %d frames\n\n', vi.num_frames);

% Read the same frame multiple times to see consistency
fprintf('Reading frame 5000 ten times:\n');
for i = 1:10
    tic;
    frame = read_ffmpeg_frame(vi, 5000);
    t = toc * 1000;
    fprintf('  %.1f ms\n', t);
end

% Read sequential frames (should be fast - no seeking needed)
fprintf('\nReading 10 sequential frames starting at 5000:\n');
tic;
for i = 5000:5009
    frame = read_ffmpeg_frame(vi, i);
end
t = toc * 1000;
fprintf('  Total: %.1f ms (%.1f ms per frame)\n', t, t/10);

% Read 10 random frames
fprintf('\nReading 10 random frames:\n');
rng(42);
indices = randperm(vi.num_frames, 10);
tic;
for i = 1:10
    frame = read_ffmpeg_frame(vi, indices(i));
end
t = toc * 1000;
fprintf('  Total: %.1f ms (%.1f ms per frame)\n', t, t/10);

close_ffmpeg_video(vi);
