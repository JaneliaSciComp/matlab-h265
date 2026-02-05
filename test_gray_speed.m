% Compare speed of grayscale vs RGB encoded files

fprintf('Testing grayscale file (movie_gray.mp4):\n');
vi_gray = open_ffmpeg_video('movie_gray.mp4');
tic;
frames = read_ffmpeg_frames(vi_gray, 5000, 5999);
t_gray = toc;
fprintf('  1000 frames in %.1f ms (%.2f ms/frame)\n', t_gray*1000, t_gray);
close_ffmpeg_video(vi_gray);

fprintf('\nTesting RGB file (movie_keyint50.mp4):\n');
vi_rgb = open_ffmpeg_video('movie_keyint50.mp4');
tic;
frames = read_ffmpeg_frames(vi_rgb, 5000, 5999);
t_rgb = toc;
fprintf('  1000 frames in %.1f ms (%.2f ms/frame)\n', t_rgb*1000, t_rgb);
close_ffmpeg_video(vi_rgb);

fprintf('\nSpeedup: %.2fx\n', t_rgb/t_gray);
