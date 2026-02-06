function avi_from_ufmf()
% Extract the first 10k frames from movie.ufmf and write to uncompressed grayscale AVI

% % Add toolbox to path
% modpath();

% Input/output files
input_file = 'movie.ufmf';
output_file = 'movie-10k-frames.avi';
num_frames = 10000;

% Read the UFMF header
header = ufmf_read_header(input_file);
header_cleanup = onCleanup(@() fclose(header.fid));

% Verify we have enough frames
if header.nframes < num_frames
  warning('File only has %d frames, extracting all of them.', header.nframes);
  num_frames = header.nframes;
end

% Calculate frame rate from timestamps of frames being extracted
timestamps = header.timestamps;
duration = timestamps(num_frames) - timestamps(1);
fps = (num_frames - 1) / duration;

% Create VideoWriter for grayscale AVI
vw = VideoWriter(output_file, 'Grayscale AVI');
vw.FrameRate = fps;
open(vw);

% Extract and write frames
fprintf('Extracting %d frames from %s to %s...\n', num_frames, input_file, output_file);
for i = 1:num_frames
  % Read frame from UFMF
  [im, ~, ~, ~, ~] = ufmf_read_frame(header, i);

  % Write frame to AVI
  writeVideo(vw, im);

  % Progress update every 1000 frames
  if mod(i, 1000) == 0
    fprintf('  Processed %d/%d frames\n', i, num_frames);
  end
end

% Cleanup: vw closes automatically, header_cleanup closes file
clear vw header_cleanup;

fprintf('Done. Output written to %s\n', output_file);

end  % function
