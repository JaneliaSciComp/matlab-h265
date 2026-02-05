% Debug frame 100 issue
vi = open_ffmpeg_video('test_output.mp4');
fprintf('num_frames: %d, pts_increment: %g\n', vi.num_frames, vi.pts_increment);
fprintf('Last 5 DTS values: %s\n', mat2str(vi.dts(end-4:end)));
fprintf('Frame 100 DTS value: %g\n', vi.dts(100));
fprintf('Frame 100 expected PTS: %g\n', 99 * vi.pts_increment);
close_ffmpeg_video(vi);
