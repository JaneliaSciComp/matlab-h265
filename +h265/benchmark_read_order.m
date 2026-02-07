function benchmark_read_order()
% BENCHMARK_READ_ORDER Compare sequential vs random frame read performance
%
%   Creates a 1000-frame test video, then compares the time to read all
%   frames sequentially (first to last) vs in random order.

% Parameters
frame_count = 500;
width = 256;
height = 256;
frame_rate = 30;
gop_size = 50;

% Create temp file
temp_file_name = [tempname() '.mp4'];
file_cleaner = onCleanup(@() delete(temp_file_name));

% Generate and write test video
fprintf('Creating %d-frame test video...\n', frame_count);
frames = uint8(randi([0 255], height, width, frame_count));
writer = h265.Writer(temp_file_name, width, height, frame_rate, 'is_gray', true, 'gop_size', gop_size);
writer.write(frames);
delete(writer);

% Open reader
reader = h265.Reader(temp_file_name, 'is_gray', true);

% Sequential read (first to last)
fprintf('Reading frames sequentially (1 to %d)...\n', frame_count);
tic_id = tic();
for i = 1:frame_count
  frame = reader.read(i); %#ok<NASGU>
end
sequential_time = toc(tic_id);

% Random order read
random_indices = randperm(frame_count);
fprintf('Reading frames in random order...\n');
tic_id_2 = tic();
for i = 1:frame_count
  frame = reader.read(random_indices(i)); %#ok<NASGU>
end
random_time = toc(tic_id_2);

% Report results
fprintf('\n');
fprintf('Results:\n');
fprintf('  Sequential read: %.3f seconds (%.1f frames/sec)\n', ...
  sequential_time, frame_count / sequential_time);
fprintf('  Random read:     %.3f seconds (%.1f frames/sec)\n', ...
  random_time, frame_count / random_time);
fprintf('  Ratio:           %.1fx faster for sequential access\n', ...
  random_time / sequential_time);
fprintf('  GOP size:        %d (expected ratio: ~%.1fx)\n', ...
  gop_size, gop_size / 2);

end
