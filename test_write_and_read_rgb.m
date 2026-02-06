function test_write_and_read_rgb()
% TEST_WRITE_AND_READ_RGB Test RGB video writing and reading
%   Generates random RGB frames, writes to H.265, reads back, and
%   verifies using SSIM (Structural Similarity Index). Throws error on failure.

% Create temp directory and ensure cleanup on exit (normal or error)
temp_dir = tempname();
mkdir(temp_dir);
cleanup = onCleanup(@() rmdir(temp_dir, 's'));

% Parameters
width = 256;
height = 256;
frame_count = 100;
frame_rate = 30;  % Hz
min_ssim = 0.8;  % Threshold for filtered data with lossy compression
output_file = fullfile(temp_dir, 'test_output.mp4');

% Generate frames with low-pass filtered random RGB values
% This creates spatially correlated data like real images
raw_frames = randi([0 255], height, width, 3, frame_count);
original_frames = uint8(imgaussfilt(double(raw_frames), 5));

% Write frames to file (RGB is the default)
writer = H265Writer(output_file, width, height, frame_rate);

% Check writer properties
assert(strcmp(writer.filename, output_file), 'Writer filename mismatch');
assert(writer.width == width, 'Writer width mismatch');
assert(writer.height == height, 'Writer height mismatch');
assert(writer.frame_rate == frame_rate, 'Writer frame_rate mismatch');
assert(writer.frames_written == 0, 'Writer frames_written should start at 0');

for i = 1:frame_count
    writer.write(original_frames(:,:,:,i));
end

% Check writer state after writing
assert(writer.frames_written == frame_count, 'Writer frames_written mismatch');
assert(abs(writer.duration() - frame_count/frame_rate) < 0.001, 'Writer duration mismatch');

clear writer;  % Flush and close

% Read back the file
reader = H265Reader(output_file);

% Check reader properties
assert(~isempty(reader.filename), 'Reader filename not set');
assert(reader.num_frames == frame_count, 'Frame count mismatch');
assert(reader.width == width, 'Reader width mismatch');
assert(reader.height == height, 'Reader height mismatch');
assert(reader.frame_rate == frame_rate, 'Reader frame_rate mismatch');
assert(reader.duration() > 0, 'Reader duration should be positive');

% Read all frames back (batch read)
readback_frames = reader.read(1, frame_count);

% Check frame type and dimensions
assert(isa(readback_frames, 'uint8'), 'Frames should be uint8');
assert(size(readback_frames, 1) == height, 'Batch frame height mismatch');
assert(size(readback_frames, 2) == width, 'Batch frame width mismatch');
assert(size(readback_frames, 3) == 3, 'Batch frame should have 3 channels');
assert(size(readback_frames, 4) == frame_count, 'Batch frame count mismatch');

% Test single-frame reads of first, middle, and last frames
first_frame = reader.read(1);
middle_idx = floor(frame_count / 2);
middle_frame = reader.read(middle_idx);
last_frame = reader.read(frame_count);

assert(isa(first_frame, 'uint8'), 'Single frame should be uint8');
assert(size(first_frame, 3) == 3, 'Single frame should have 3 channels');
assert(isequal(first_frame, readback_frames(:,:,:,1)), 'Single read of first frame does not match batch read');
assert(isequal(middle_frame, readback_frames(:,:,:,middle_idx)), 'Single read of middle frame does not match batch read');
assert(isequal(last_frame, readback_frames(:,:,:,frame_count)), 'Single read of last frame does not match batch read');

clear reader;

% Compute SSIM for each frame (average across channels)
ssim_values = zeros(frame_count, 1);
for i = 1:frame_count
    ssim_r = ssim(readback_frames(:,:,1,i), original_frames(:,:,1,i));
    ssim_g = ssim(readback_frames(:,:,2,i), original_frames(:,:,2,i));
    ssim_b = ssim(readback_frames(:,:,3,i), original_frames(:,:,3,i));
    ssim_values(i) = (ssim_r + ssim_g + ssim_b) / 3;
end

min_ssim_actual = min(ssim_values);

% Check SSIM threshold
if min_ssim_actual < min_ssim
    error('test_write_and_read_rgb:ssim', 'SSIM too low: minimum %.4f < threshold %.4f', min_ssim_actual, min_ssim);
end

% Test second file with different parameters
output_file2 = fullfile(temp_dir, 'test_output2.mp4');
width2 = 128;
height2 = 128;
frame_rate2 = 24;
frame_count2 = 10;

writer2 = H265Writer(output_file2, width2, height2, frame_rate2);
for i = 1:frame_count2
    writer2.write(uint8(randi([0 255], height2, width2, 3)));
end
clear writer2;

reader2 = H265Reader(output_file2);
assert(reader2.num_frames == frame_count2, 'Second file frame count mismatch');
assert(reader2.width == width2, 'Second file width mismatch');
assert(reader2.height == height2, 'Second file height mismatch');
assert(reader2.frame_rate == frame_rate2, 'Second file frame_rate mismatch');
clear reader2;

end
