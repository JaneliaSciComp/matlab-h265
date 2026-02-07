function test_memory_leaks(varargin)
% TEST_MEMORY_LEAKS Check for memory leaks in H265Reader cache implementation
%
% This test:
%   1. Creates a test video with many GOPs
%   2. Does a warmup to stabilize one-time allocations
%   3. Runs many cycles with random frame reads (to trigger cache misses)
%   4. Computes memory growth per cycle and checks against threshold
%
% Optional parameters:
%   do_debug - if true, print details and plot memory vs cycle (default: false)
%
% Errors if memory grows faster than the threshold.

do_debug = myparse(varargin, 'do_debug', false);

test_file = create_test_video();
test_cleanup = onCleanup(@() delete(test_file)); %#ok<NASGU>

% Get video info
reader = H265Reader(test_file, 'is_gray', true);
num_frames = reader.num_frames;
reader = []; %#ok<NASGU>

if do_debug
  fprintf('Test video: %d frames, measuring memory during warmup and test cycles\n', num_frames);
end

% Warmup: run enough cycles to stabilize one-time allocations
warmup_cycle_count = 100;
warmup_memory_samples = zeros(1, warmup_cycle_count);
for warmup_cycle_index = 1:warmup_cycle_count
  reader = H265Reader(test_file, 'is_gray', true);
  for j = 1:20
    frame = reader.read(randi(num_frames)); %#ok<NASGU>
  end
  reader = []; %#ok<NASGU>
  warmup_memory_samples(warmup_cycle_index) = get_memory_kb();
end

if do_debug
  fprintf('Warmup: %d cycles\n', warmup_cycle_count);
  fprintf('  Start: %.1f MB, End: %.1f MB\n', warmup_memory_samples(1)/1024, warmup_memory_samples(end)/1024);
end

% Run cycles, measuring memory after each
cycle_count = 200;
memory_samples = zeros(1, cycle_count);
for cycle_index = 1:cycle_count
  reader = H265Reader(test_file, 'is_gray', true);
  for j = 1:20
    frame = reader.read(randi(num_frames)); %#ok<NASGU>
  end
  reader = [];  %#ok<NASGU> Release handle to trigger destructor
  memory_samples(cycle_index) = get_memory_kb();
end

% Compute memory growth per cycle
memory_delta_kb = memory_samples(end) - memory_samples(1);
kb_per_cycle = memory_delta_kb / cycle_count;

% Threshold: 10 KB per cycle would be 1 MB per 100 cycles
max_kb_per_cycle = 10;

if do_debug
  fprintf('Test: %d cycles, %.2f KB/cycle (threshold: %.1f)\n', ...
    cycle_count, kb_per_cycle, max_kb_per_cycle);
  fprintf('  Start: %.1f MB, End: %.1f MB, Delta: %.1f KB\n', ...
    memory_samples(1)/1024, memory_samples(end)/1024, memory_delta_kb);

  figure('Name', 'Memory Leak Test');

  subplot(2, 1, 1);
  plot(1:warmup_cycle_count, warmup_memory_samples / 1024, 'b.-');
  xlabel('Warmup Cycle');
  ylabel('Memory (MB)');
  title('Warmup Phase');
  grid on;

  subplot(2, 1, 2);
  plot(1:cycle_count, memory_samples / 1024, 'b.-');
  xlabel('Test Cycle');
  ylabel('Memory (MB)');
  title(sprintf('Test Phase: %.2f KB/cycle (threshold: %.1f)', kb_per_cycle, max_kb_per_cycle));
  grid on;
end

if kb_per_cycle > max_kb_per_cycle
  error('test_memory_leaks:leak', ...
    'Memory leak detected: %.2f KB/cycle (threshold: %.1f KB/cycle)', ...
    kb_per_cycle, max_kb_per_cycle);
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
% CREATE_TEST_VIDEO Create a test video with many GOPs

filename = [tempname '.mp4'];
width = 128;
height = 128;
frame_count = 1000;
frame_rate = 30;
gop_size = 30;

frames = uint8(randi([0 255], height, width, frame_count));

writer = H265Writer(filename, width, height, frame_rate, 'is_gray', true, 'gop_size', gop_size);
writer.write(frames);
writer = [];  %#ok<NASGU> Release handle to trigger destructor

end  % function
