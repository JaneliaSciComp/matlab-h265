% ufmf_to_avi.m
% Extract the first 10k frames from movie.ufmf and write to uncompressed grayscale AVI

% % Add toolbox to path
% modpath();

% Input/output files
input_file = 'movie.ufmf';
output_file = 'movie.avi';
num_frames = 10000;

% Read the UFMF header
header = ufmf_read_header(input_file);

% Verify we have enough frames
if header.nframes < num_frames
    warning('File only has %d frames, extracting all of them.', header.nframes);
    num_frames = header.nframes;
end

% Calculate frame rate from timestamps
timestamps = header.timestamps;
duration = timestamps(end) - timestamps(1);
fps = (header.nframes - 1) / duration;

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

% Clean up
close(vw);
fclose(header.fid);

fprintf('Done. Output written to %s\n', output_file);
