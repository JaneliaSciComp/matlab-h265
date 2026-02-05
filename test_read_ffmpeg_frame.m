function test_read_ffmpeg_frame(filename, num_frames_to_test)
% TEST_READ_FFMPEG_FRAME Test that read_ffmpeg_frame returns consistent results
%
% Reads random frames twice in different orders and verifies they match.
%
% Usage: test_read_ffmpeg_frame(filename, num_frames_to_test)
%   filename          - path to video file (default: 'movie.mp4')
%   num_frames_to_test - number of frames to test (default: 100)

if nargin < 1 || isempty(filename)
    filename = 'movie.mp4';
end
if nargin < 2 || isempty(num_frames_to_test)
    num_frames_to_test = 100;
end

% Get total frame count by reading frame 1 to get dimensions
frame1 = read_ffmpeg_frame(filename, 1);
[height, width] = size(frame1);

% For this test, assume 10000 frames (from our earlier conversion)
total_frames = 10000;

% Generate random frame indices
rng(42);  % For reproducibility
frame_indices = randperm(total_frames, num_frames_to_test);

fprintf('Testing %d random frames from %s\n', num_frames_to_test, filename);
fprintf('Frame dimensions: %d x %d\n', height, width);

% First pass: read frames in original order
fprintf('Pass 1: Reading frames in original order...\n');
frames1 = zeros(height, width, num_frames_to_test, 'uint8');
tic;
for i = 1:num_frames_to_test
    frames1(:,:,i) = read_ffmpeg_frame(filename, frame_indices(i));
    if mod(i, 25) == 0
        fprintf('  Read %d/%d frames\n', i, num_frames_to_test);
    end
end
time1 = toc;
fprintf('  Pass 1 complete in %.2f seconds\n', time1);

% Shuffle the order
shuffle_order = randperm(num_frames_to_test);
shuffled_indices = frame_indices(shuffle_order);

% Second pass: read frames in shuffled order
fprintf('Pass 2: Reading frames in shuffled order...\n');
frames2 = zeros(height, width, num_frames_to_test, 'uint8');
tic;
for i = 1:num_frames_to_test
    frames2(:,:,shuffle_order(i)) = read_ffmpeg_frame(filename, shuffled_indices(i));
    if mod(i, 25) == 0
        fprintf('  Read %d/%d frames\n', i, num_frames_to_test);
    end
end
time2 = toc;
fprintf('  Pass 2 complete in %.2f seconds\n', time2);

% Compare frames
fprintf('Comparing frames...\n');
all_match = true;
for i = 1:num_frames_to_test
    if ~isequal(frames1(:,:,i), frames2(:,:,i))
        fprintf('  MISMATCH at frame index %d (frame %d)\n', i, frame_indices(i));
        all_match = false;
    end
end

if all_match
    fprintf('SUCCESS: All %d frames match bit-for-bit.\n', num_frames_to_test);
else
    fprintf('FAILURE: Some frames did not match.\n');
end

end
