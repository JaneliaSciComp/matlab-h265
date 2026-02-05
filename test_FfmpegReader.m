% Test FfmpegReader class
fprintf('Testing FfmpegReader class...\n');

% Open video
vid = FfmpegReader('test_output.mp4');
fprintf('Opened: %s\n', vid.filename);
fprintf('Frames: %d, Size: %dx%d\n', vid.num_frames, vid.width, vid.height);
fprintf('Frame rate: %.2f fps\n', vid.frame_rate());
fprintf('Duration: %.2f seconds\n', vid.duration());

% Read single frame
frame1 = vid.read(1);
fprintf('Read frame 1: %dx%d, mean=%.1f\n', size(frame1,1), size(frame1,2), mean(frame1(:)));

% Read batch
frames = vid.read(1, 10);
fprintf('Read frames 1-10: %dx%dx%d\n', size(frames,1), size(frames,2), size(frames,3));

% Test auto-close via destructor
clear vid;
fprintf('Cleared vid - destructor should have closed it\n');

fprintf('FfmpegReader test PASSED\n');
