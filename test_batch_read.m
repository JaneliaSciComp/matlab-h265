function test_batch_read()
% TEST_BATCH_READ Test that batch frame reading matches individual reads
%   Throws error on failure.

% This test requires test_output.mp4 from test_write
if ~isfile('test_output.mp4')
    error('test_batch_read:missingFile', 'test_output.mp4 not found. Run test_write first.');
end

vi = open_ffmpeg_video('test_output.mp4');

% Test reading sequential frames
start_frame = 1;
end_frame = min(20, vi.num_frames);
num_frames = end_frame - start_frame + 1;

% Method 1: Individual reads
frames1 = zeros(vi.height, vi.width, num_frames, 'uint8');
for i = 1:num_frames
    frames1(:,:,i) = read_ffmpeg_frame(vi, start_frame + i - 1);
end

% Method 2: Batch read
frames2 = read_ffmpeg_frames(vi, start_frame, end_frame);

close_ffmpeg_video(vi);

% Verify results match
if ~isequal(frames1, frames2)
    diff_count = sum(frames1(:) ~= frames2(:));
    error('test_batch_read:mismatch', 'Batch read does not match individual reads: %d pixels differ', diff_count);
end

end
