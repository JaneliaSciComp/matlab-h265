% Test closed GOP video
vi = open_ffmpeg_video('movie_closed_gop.mp4');
fprintf('Opened successfully: %d frames, %dx%d\n', vi.num_frames, vi.width, vi.height);

% Test reading frame 7075 (the one that failed before)
frame = read_ffmpeg_frame(vi, 7075);
fprintf('Read frame 7075: %dx%d\n', size(frame,1), size(frame,2));
fprintf('Success!\n');
