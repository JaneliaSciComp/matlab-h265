function test_write()
% TEST_WRITE Test video writing and reading with MEX functions
%   Generates random frames, writes to H.265, reads back, and verifies
%   using SSIM (Structural Similarity Index). Throws error on failure.

% Parameters
col_count = 256;
row_count = 256;
frame_count = 100;
frame_rate = 30;  % Hz
min_ssim = 0.8;  % Minimum acceptable SSIM (low threshold for random noise)
output_file_name = 'test_output.mp4';

% Generate all frames with random values
fprintf('Generating %d random frames (%dx%d)...\n', frame_count, row_count, col_count);
original_frames = uint8(randi([0 255], row_count, col_count, frame_count));

% Write frames to file
fprintf('Writing to %s at %d fps...\n', output_file_name, frame_rate);
writer = open_h265_write(output_file_name, col_count, row_count, frame_rate);
for i = 1:frame_count
    write_h265_frame(writer, original_frames(:,:,i));
end
close_h265_write(writer);
fprintf('Wrote %d frames\n', frame_count);

% Read back the file
fprintf('\nReading back...\n');
vi = open_ffmpeg_video(output_file_name);

% Check frame count
if vi.num_frames ~= frame_count
    close_ffmpeg_video(vi);
    error('test_write:frameCount', 'Frame count mismatch: expected %d, got %d', frame_count, vi.num_frames);
end

% Check dimensions
if vi.width ~= col_count || vi.height ~= row_count
    close_ffmpeg_video(vi);
    error('test_write:dimensions', 'Dimension mismatch: expected %dx%d, got %dx%d', col_count, row_count, vi.width, vi.height);
end

% Check frame rate
% pts_increment = (time_base_den * frame_rate_den) / (time_base_num * frame_rate_num)
actual_frame_rate = vi.frame_rate_num / vi.frame_rate_den;
if actual_frame_rate ~= frame_rate
    close_ffmpeg_video(vi);
    error('test_write:frameRate', 'Frame rate mismatch: expected %g, got %g', frame_rate, actual_frame_rate);
end

% Verify pts_increment is consistent with time_base and frame_rate
expected_pts_increment = (vi.time_base_den * vi.frame_rate_den) / (vi.time_base_num * vi.frame_rate_num);
if vi.pts_increment ~= expected_pts_increment
    close_ffmpeg_video(vi);
    error('test_write:ptsIncrement', 'pts_increment mismatch: expected %g, got %g', expected_pts_increment, vi.pts_increment);
end
fprintf('Frame count, dimensions, and frame rate (%g fps) OK\n', actual_frame_rate);

% Read all frames back
readback_frames = uint8(zeros(row_count, col_count, frame_count));
for i = 1:frame_count
    readback_frames(:,:,i) = read_ffmpeg_frame(vi, i);
end
close_ffmpeg_video(vi);

% Compute SSIM for each frame
ssim_values = zeros(frame_count, 1);
for i = 1:frame_count
    ssim_values(i) = ssim(readback_frames(:,:,i), original_frames(:,:,i));
end

mean_ssim = mean(ssim_values);
min_ssim_actual = min(ssim_values);
max_ssim = max(ssim_values);

fprintf('\nSSIM: min=%.4f, mean=%.4f, max=%.4f\n', min_ssim_actual, mean_ssim, max_ssim);

% Check SSIM threshold
if min_ssim_actual < min_ssim
    error('test_write:ssim', 'SSIM too low: minimum %.4f < threshold %.4f', min_ssim_actual, min_ssim);
end

% Report file size
d = dir(output_file_name);
fprintf('File size: %.1f KB (%.2f bits/pixel)\n', d.bytes/1024, d.bytes*8/(col_count*row_count*frame_count));

fprintf('\ntest_write PASSED\n');
end
