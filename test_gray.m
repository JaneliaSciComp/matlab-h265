vi = open_ffmpeg_video('movie_gray.mp4');
fprintf('Opened: %d frames, %dx%d\n', vi.num_frames, vi.width, vi.height);
frame = read_ffmpeg_frame(vi, 5000);
fprintf('Frame 5000: %dx%d, class %s\n', size(frame,1), size(frame,2), class(frame));
close_ffmpeg_video(vi);
