function test_H265Writer()
% TEST_H265WRITER Test H265Writer class
%   Throws error on failure.

width = 256;
height = 256;
num_frames = 50;
frame_rate = 30;
output_file = 'test_writer_output.mp4';
output_file2 = 'test_writer_output2.mp4';

% Clean up any leftover files
if isfile(output_file), delete(output_file); end
if isfile(output_file2), delete(output_file2); end

% Create writer
vid = H265Writer(output_file, width, height, frame_rate);
assert(strcmp(vid.filename, output_file), 'Filename mismatch');
assert(vid.width == width, 'Width mismatch');
assert(vid.height == height, 'Height mismatch');
assert(vid.frame_rate == frame_rate, 'Frame rate mismatch');
assert(vid.frames_written == 0, 'Initial frames_written should be 0');

% Write frames
for i = 1:num_frames
    frame = uint8(randi([0 255], height, width));
    vid.write(frame);
end
assert(vid.frames_written == num_frames, 'frames_written mismatch after writing');
assert(abs(vid.duration() - num_frames/frame_rate) < 0.001, 'Duration mismatch');

% Close via destructor
clear vid;

% Verify by reading back
reader = FfmpegReader(output_file);
assert(reader.num_frames == num_frames, 'Frame count mismatch on readback');
assert(reader.width == width, 'Width mismatch on readback');
assert(reader.height == height, 'Height mismatch on readback');
clear reader;

% Test second file with different parameters
vid2 = H265Writer(output_file2, 128, 128, 24);
vid2.write(uint8(zeros(128, 128)));
vid2.write(uint8(ones(128, 128) * 255));
clear vid2;

% Verify second file
reader2 = FfmpegReader(output_file2);
assert(reader2.num_frames == 2, 'Second file frame count mismatch');
assert(reader2.width == 128, 'Second file width mismatch');
assert(reader2.height == 128, 'Second file height mismatch');
clear reader2;

% Cleanup test files
delete(output_file);
delete(output_file2);

end
