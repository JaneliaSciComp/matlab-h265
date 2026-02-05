function test_read_ffmpeg_frame()
% TEST_READ_FFMPEG_FRAME Test that read_ffmpeg_frame returns consistent results
%   Reads random frames twice in different orders and verifies they match.
%   Throws error on failure.

% This test requires test_output.mp4 from test_write
if ~isfile('test_output.mp4')
    error('test_read_ffmpeg_frame:missingFile', 'test_output.mp4 not found. Run test_write first.');
end

filename = 'test_output.mp4';
num_frames_to_test = 50;

vi = open_ffmpeg_video(filename);
total_frames = vi.num_frames;
height = vi.height;
width = vi.width;

% Generate random frame indices
rng(42);
frame_indices = randperm(total_frames, num_frames_to_test);

% First pass: read frames in original order
frames1 = zeros(height, width, num_frames_to_test, 'uint8');
for i = 1:num_frames_to_test
    frames1(:,:,i) = read_ffmpeg_frame(vi, frame_indices(i));
end

% Shuffle the order
shuffle_order = randperm(num_frames_to_test);
shuffled_indices = frame_indices(shuffle_order);

% Second pass: read frames in shuffled order
frames2 = zeros(height, width, num_frames_to_test, 'uint8');
for i = 1:num_frames_to_test
    frames2(:,:,shuffle_order(i)) = read_ffmpeg_frame(vi, shuffled_indices(i));
end

close_ffmpeg_video(vi);

% Compare frames
for i = 1:num_frames_to_test
    if ~isequal(frames1(:,:,i), frames2(:,:,i))
        error('test_read_ffmpeg_frame:mismatch', 'Frame %d (index %d) does not match', i, frame_indices(i));
    end
end

end
