function benchmark_read_order()
% BENCHMARK_READ_ORDER Compare access patterns for MP4 and UFMF formats

frame_count = 1000;

% Test MP4 files
mp4_files = dir('mp4-versions/*.mp4');
if ~isempty(mp4_files)
  mp4_path = fullfile(mp4_files(1).folder, mp4_files(1).name);
  fprintf('=== MP4: %s ===\n', mp4_files(1).name);
  benchmark_mp4(mp4_path, frame_count);
end

fprintf('\n');

% Test UFMF files
ufmf_files = dir('ufmf-trimmed/*.ufmf');
if ~isempty(ufmf_files)
  ufmf_path = fullfile(ufmf_files(1).folder, ufmf_files(1).name);
  fprintf('=== UFMF: %s ===\n', ufmf_files(1).name);
  benchmark_ufmf(ufmf_path, frame_count);
end

end % function

function benchmark_mp4(mp4_path, frame_count)
  reader = h265.Reader(mp4_path);
  frame_count = min(frame_count, reader.num_frames);

  % Batch read
  tic; frames_batch = reader.read(1, frame_count); batch_time = toc; %#ok<NASGU>

  % Sequential
  tic;
  for i = 1:frame_count
    frame = reader.read(i); %#ok<NASGU>
  end
  sequential_time = toc;

  % Random walk (50% forward, 50% back) - start in middle for uniform coverage
  walk_pos = round(frame_count / 2);
  tic;
  for i = 1:frame_count
    frame = reader.read(walk_pos); %#ok<NASGU>
    if rand() < 0.5
      walk_pos = min(walk_pos + 1, frame_count);
    else
      walk_pos = max(walk_pos - 1, 1);
    end
  end
  random_walk_time = toc;

  % Fully random
  random_indices = randperm(frame_count);
  tic;
  for i = 1:frame_count
    frame = reader.read(random_indices(i)); %#ok<NASGU>
  end
  random_time = toc;

  delete(reader);

  fprintf('  Batch read:   %.2f ms/frame\n', 1000 * batch_time / frame_count);
  fprintf('  Random walk:  %.2f ms/frame\n', 1000 * random_walk_time / frame_count);
  fprintf('  Sequential:   %.2f ms/frame\n', 1000 * sequential_time / frame_count);
  fprintf('  Fully random: %.2f ms/frame\n', 1000 * random_time / frame_count);
end % function

function benchmark_ufmf(ufmf_path, frame_count)
  header = ufmf_read_header(ufmf_path);
  cleanup = onCleanup(@() fclose(header.fid));
  frame_count = min(frame_count, header.nframes);

  % Batch read
  tic;
  frames_batch = zeros(header.nr, header.nc, frame_count, 'uint8');
  for i = 1:frame_count
    frames_batch(:,:,i) = ufmf_read_frame(header, i);
  end
  batch_time = toc;

  % Sequential
  tic;
  for i = 1:frame_count
    frame = ufmf_read_frame(header, i); %#ok<NASGU>
  end
  sequential_time = toc;

  % Random walk (50% forward, 50% back) - start in middle for uniform coverage
  walk_pos = round(frame_count / 2);
  tic;
  for i = 1:frame_count
    frame = ufmf_read_frame(header, walk_pos); %#ok<NASGU>
    if rand() < 0.5
      walk_pos = min(walk_pos + 1, frame_count);
    else
      walk_pos = max(walk_pos - 1, 1);
    end
  end
  random_walk_time = toc;

  % Fully random
  random_indices = randperm(frame_count);
  tic;
  for i = 1:frame_count
    frame = ufmf_read_frame(header, random_indices(i)); %#ok<NASGU>
  end
  random_time = toc;

  fprintf('  Batch read:   %.2f ms/frame\n', 1000 * batch_time / frame_count);
  fprintf('  Random walk:  %.2f ms/frame\n', 1000 * random_walk_time / frame_count);
  fprintf('  Sequential:   %.2f ms/frame\n', 1000 * sequential_time / frame_count);
  fprintf('  Fully random: %.2f ms/frame\n', 1000 * random_time / frame_count);
end % function
