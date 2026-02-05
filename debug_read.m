% Debug read issue
vi = open_ffmpeg_video('test_output.mp4');
fprintf('num_frames: %d\n', vi.num_frames);
fprintf('First 5 DTS values: %s\n', mat2str(vi.dts(1:min(5,end))));
fprintf('Trying to read frame 1...\n');
frame1 = read_ffmpeg_frame(vi, 1);
close_ffmpeg_video(vi);
