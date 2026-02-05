% Test FFmpeg reader overhead breakdown

vi = open_ffmpeg_video('movie_keyint10.mp4');

% Time just opening the file (what read_ffmpeg_frame does internally)
fprintf('Testing overhead components...\n\n');

% Full read_ffmpeg_frame calls
rng(42);
indices = randperm(vi.num_frames, 100);
times = zeros(100, 1);
for i = 1:100
    tic;
    frame = read_ffmpeg_frame(vi, indices(i));
    times(i) = toc * 1000;
end
fprintf('read_ffmpeg_frame (keyint=10): mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));

% Compare to keyint=50
vi50 = open_ffmpeg_video('movie_keyint50.mp4');
for i = 1:100
    tic;
    frame = read_ffmpeg_frame(vi50, indices(i));
    times(i) = toc * 1000;
end
fprintf('read_ffmpeg_frame (keyint=50): mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));

% For reference, how long does just open_ffmpeg_video take?
times = zeros(20, 1);
for i = 1:20
    tic;
    vi_tmp = open_ffmpeg_video('movie_keyint10.mp4');
    times(i) = toc * 1000;
end
fprintf('\nopen_ffmpeg_video alone:       mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));
