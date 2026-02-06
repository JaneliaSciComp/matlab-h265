function test_memory_leaks()
% TEST_MEMORY_LEAKS Check for memory leaks in H265Reader cache implementation
%
% This test:
%   1. Creates a test video with multiple GOPs
%   2. Repeatedly opens/reads/closes the video
%   3. Monitors memory usage to detect leaks
%
% A significant upward trend in memory usage indicates a leak.

fprintf('Testing for memory leaks...\n');

% Create a test video with multiple GOPs
test_file = create_test_video();
test_cleanup = onCleanup(@() delete(test_file));

% Get baseline memory
pause(0.1);  % Let system settle
baseline_mem = get_memory_kb();
fprintf('Baseline memory: %d KB\n', baseline_mem);

% Test 1: Repeated open/close cycles
fprintf('\nTest 1: Open/close cycles (no reads)...\n');
num_cycles = 50;
for i = 1:num_cycles
  reader = H265Reader(test_file, 'is_gray', true);
  clear reader;
end
mem_after_open_close = get_memory_kb();
fprintf('  After %d open/close cycles: %d KB (delta: %+d KB)\n', ...
  num_cycles, mem_after_open_close, mem_after_open_close - baseline_mem);

% Test 2: Repeated read cycles within same reader (sequential access)
fprintf('\nTest 2: Read cycles within same reader (sequential)...\n');
reader = H265Reader(test_file, 'is_gray', true);
num_reads = reader.num_frames;
for i = 1:num_reads
  frame = reader.read(i);
end
clear reader;
mem_after_reads = get_memory_kb();
fprintf('  After %d sequential reads: %d KB (delta: %+d KB)\n', ...
  num_reads, mem_after_reads, mem_after_reads - baseline_mem);

% Test 3: Repeated full cycles (open, read sequential frames, close)
fprintf('\nTest 3: Full open/read/close cycles...\n');
num_full_cycles = 20;
for i = 1:num_full_cycles
  reader = H265Reader(test_file, 'is_gray', true);
  % Read first 50 frames (spans multiple GOPs)
  for j = 1:min(50, reader.num_frames)
    frame = reader.read(j);
  end
  clear reader;
end
mem_after_full = get_memory_kb();
fprintf('  After %d full cycles: %d KB (delta: %+d KB)\n', ...
  num_full_cycles, mem_after_full, mem_after_full - baseline_mem);

% Test 4: Stress test - many cycles to amplify any small leaks
fprintf('\nTest 4: Stress test (100 full cycles)...\n');
mem_samples = zeros(1, 10);
for batch = 1:10
  for i = 1:10
    reader = H265Reader(test_file, 'is_gray', true);
    % Read first 20 frames
    for j = 1:min(20, reader.num_frames)
      frame = reader.read(j);
    end
    clear reader;
  end
  mem_samples(batch) = get_memory_kb();
end
fprintf('  Memory samples across batches:\n');
fprintf('    ');
fprintf('%d ', mem_samples);
fprintf('KB\n');

% Analyze trend
mem_trend = polyfit(1:10, mem_samples, 1);
fprintf('  Memory trend: %+.1f KB per 10 cycles\n', mem_trend(1));

% Final assessment
final_mem = get_memory_kb();
total_delta = final_mem - baseline_mem;
fprintf('\n========================================\n');
fprintf('Final memory: %d KB (total delta: %+d KB)\n', final_mem, total_delta);

% Check for concerning trends
if mem_trend(1) > 100
  fprintf('WARNING: Significant upward memory trend detected!\n');
  fprintf('         This may indicate a memory leak.\n');
elseif total_delta > 10000
  fprintf('WARNING: Large total memory increase detected!\n');
  fprintf('         This may indicate a memory leak.\n');
else
  fprintf('PASS: No significant memory leaks detected.\n');
end
fprintf('========================================\n');

end  % function


function mem_kb = get_memory_kb()
% GET_MEMORY_KB Get current process memory usage in KB
%
% Uses /proc/self/status on Linux to get VmRSS (resident set size)

if ispc
  % Windows: use memory command
  m = memory;
  mem_kb = m.MemUsedMATLAB / 1024;
else
  % Linux: read from /proc/self/status
  fid = fopen('/proc/self/status', 'r');
  status_cleanup = onCleanup(@() fclose(fid));
  content = fread(fid, inf, '*char')';

  % Find VmRSS line (resident set size)
  match = regexp(content, 'VmRSS:\s*(\d+)\s*kB', 'tokens');
  if ~isempty(match)
    mem_kb = str2double(match{1}{1});
  else
    % Fallback: try VmSize
    match = regexp(content, 'VmSize:\s*(\d+)\s*kB', 'tokens');
    if ~isempty(match)
      mem_kb = str2double(match{1}{1});
    else
      mem_kb = 0;
      warning('Could not read memory from /proc/self/status');
    end
  end
end

end  % function


function filename = create_test_video()
% CREATE_TEST_VIDEO Create a test video with multiple GOPs

filename = [tempname '.mp4'];
width = 128;
height = 128;
num_frames = 200;  % Multiple GOPs with default gop_size=50
frame_rate = 30;

% Generate grayscale frames
frames = uint8(randi([0 255], height, width, num_frames));

% Write video
writer = H265Writer(filename, width, height, frame_rate, 'is_gray', true, 'gop_size', 30);
writer.write(frames);
clear writer;

fprintf('Created test video: %s (%d frames, ~%d GOPs)\n', ...
  filename, num_frames, ceil(num_frames / 30));

end  % function
