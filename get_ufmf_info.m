% get_ufmf_info.m
% Script to determine the number of rows, cols, and frames in movie.ufmf

% % Add toolbox to path
% modpath();

% Read the UFMF header
header = ufmf_read_header('movie.ufmf');

% Extract dimensions and frame count
rows = header.nr;
cols = header.nc;
frames = header.nframes;

% Calculate frame rate from timestamps
timestamps = header.timestamps;
duration = timestamps(end) - timestamps(1);
fps = (frames - 1) / duration;

% Display results
fprintf('UFMF File: movie.ufmf\n');
fprintf('Rows (height): %d\n', rows);
fprintf('Cols (width):  %d\n', cols);
fprintf('Frames:        %d\n', frames);
fprintf('Frame rate:    %.2f fps\n', fps);
