% Test H265Writer class
fprintf('Testing H265Writer class...\n');

width = 256;
height = 256;
num_frames = 50;
frame_rate = 30;

% Create writer
vid = H265Writer('test_writer_output.mp4', width, height, frame_rate);
fprintf('Opened: %s (%dx%d @ %g fps)\n', vid.filename, vid.width, vid.height, vid.frame_rate);

% Write frames
for i = 1:num_frames
    frame = uint8(randi([0 255], height, width));
    vid.write(frame);
end
fprintf('Wrote %d frames, duration: %.2f seconds\n', vid.frames_written, vid.duration());

% Close via destructor
clear vid;
fprintf('Cleared vid - destructor should have closed it\n');

% Verify by reading back
reader = FfmpegReader('test_writer_output.mp4');
fprintf('Read back: %d frames, %dx%d\n', reader.num_frames, reader.width, reader.height);
assert(reader.num_frames == num_frames, 'Frame count mismatch');
clear reader;

% Test second file
vid2 = H265Writer('test_writer_output2.mp4', 128, 128, 24);
vid2.write(uint8(zeros(128, 128)));
vid2.write(uint8(ones(128, 128) * 255));
clear vid2;

% Verify second file
reader2 = FfmpegReader('test_writer_output2.mp4');
assert(reader2.num_frames == 2, 'Second file frame count mismatch');
clear reader2;

% Cleanup test files
delete('test_writer_output.mp4');
delete('test_writer_output2.mp4');

fprintf('H265Writer test PASSED\n');
