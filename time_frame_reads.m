% time_frame_reads.m
% Time how long it takes to read 10 random frames

filename = 'movie.mp4';
total_frames = 10000;

rng(42);
frame_indices = randperm(total_frames, 10);

fprintf('Timing reads of 10 random frames from %s\n\n', filename);
fprintf('Frame#    Time (s)\n');
fprintf('------    --------\n');

for i = 1:10
    idx = frame_indices(i);
    tic;
    frame = read_ffmpeg_frame(filename, idx);
    t = toc;
    fprintf('%6d    %8.3f\n', idx, t);
end
