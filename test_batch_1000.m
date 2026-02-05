vi = open_ffmpeg_video('movie_keyint10.mp4');
fprintf('Reading 1000 frames (5000-5999) in batch...\n');
tic;
frames = read_ffmpeg_frames(vi, 5000, 5999);
t = toc;
fprintf('Total: %.1f ms\n', t*1000);
fprintf('Per frame: %.2f ms\n', t*1000/1000);
fprintf('Output size: %dx%dx%d\n', size(frames,1), size(frames,2), size(frames,3));
close_ffmpeg_video(vi);
