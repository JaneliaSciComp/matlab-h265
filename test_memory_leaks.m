function test_memory_leaks()
% TEST_MEMORY_LEAKS Check for memory leaks in H265Reader cache implementation
%
% This test:
%   1. Creates a test video with multiple GOPs
%   2. Repeatedly opens/reads/closes the video
%   3. Monitors memory usage to detect leaks
%
% Errors if a significant upward trend in memory usage is detected.

% Create a test video with multiple GOPs
test_file = create_test_video();
test_cleanup = onCleanup(@() delete(test_file));

% Get baseline memory
pause(0.1);
baseline_memory = get_memory_kb();

% Test 1: Repeated open/close cycles
num_cycles = 50;
for i = 1:num_cycles
  reader = H265Reader(test_file, 'is_gray', true);
  delete(reader);
end

% Test 2: Repeated read cycles within same reader (sequential access)
reader = H265Reader(test_file, 'is_gray', true);
frame_count = reader.num_frames;
for i = 1:frame_count
  frame = reader.read(i);
end
delete(reader);

% Test 3: Repeated full cycles (open, read sequential frames, close)
num_full_cycles = 20;
for i = 1:num_full_cycles
  reader = H265Reader(test_file, 'is_gray', true);
  for j = 1:min(50, reader.num_frames)
    frame = reader.read(j);
  end
  delete(reader);
end

% Test 4: Stress test - many cycles to amplify any small leaks
memory_samples = zeros(1, 10);
for batch = 1:10
  for i = 1:10
    reader = H265Reader(test_file, 'is_gray', true);
    for j = 1:min(20, reader.num_frames)
      frame = reader.read(j);
    end
    delete(reader);
  end
  memory_samples(batch) = get_memory_kb();
end

% Analyze trend
memory_trend = polyfit(1:10, memory_samples, 1);
memory_slope = memory_trend(1);

final_memory = get_memory_kb();
total_delta = final_memory - baseline_memory;

% Thresholds for failure
max_trend_kb_per_10_cycles = 100;
max_total_delta_kb = 50000;

if memory_slope > max_trend_kb_per_10_cycles
  error('test_memory_leaks:trend', ...
    'Significant upward memory trend: %.1f KB per 10 cycles (threshold: %d KB)', ...
    memory_slope, max_trend_kb_per_10_cycles);
end

if total_delta > max_total_delta_kb
  error('test_memory_leaks:total', ...
    'Large total memory increase: %d KB (threshold: %d KB)', ...
    total_delta, max_total_delta_kb);
end

end  % function


function memory_kb = get_memory_kb()
% GET_MEMORY_KB Get current process memory usage in KB

if ispc
  memory_info = memory;
  memory_kb = memory_info.MemUsedMATLAB / 1024;
else
  file_handle = fopen('/proc/self/status', 'r');
  file_cleanup = onCleanup(@() fclose(file_handle));
  content = fread(file_handle, inf, '*char')';

  match = regexp(content, 'VmRSS:\s*(\d+)\s*kB', 'tokens');
  if ~isempty(match)
    memory_kb = str2double(match{1}{1});
  else
    match = regexp(content, 'VmSize:\s*(\d+)\s*kB', 'tokens');
    if ~isempty(match)
      memory_kb = str2double(match{1}{1});
    else
      error('get_memory_kb:read', 'Could not read memory from /proc/self/status');
    end
  end
end

end  % function


function filename = create_test_video()
% CREATE_TEST_VIDEO Create a test video with multiple GOPs

filename = [tempname '.mp4'];
width = 128;
height = 128;
frame_count = 200;
frame_rate = 30;
gop_size = 30;

frames = uint8(randi([0 255], height, width, frame_count));

writer = H265Writer(filename, width, height, frame_rate, 'is_gray', true, 'gop_size', gop_size);
writer.write(frames);
delete(writer);

end  % function
