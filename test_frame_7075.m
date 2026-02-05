% Test reading frame 7075 with debug output
vi = open_ffmpeg_video('movie.mp4');
fprintf('Frame 7075: dts=%d, file_offset=%d\n', vi.dts(7075), vi.file_offset(7075));
fprintf('Frame 7076: dts=%d, file_offset=%d\n', vi.dts(7076), vi.file_offset(7076));

% Show nearby frames
fprintf('\nNearby frames:\n');
for i = 7073:7078
    fprintf('  Frame %d: dts=%d, file_offset=%d\n', i, vi.dts(i), vi.file_offset(i));
end

% Try to read frame 7075
fprintf('\nAttempting to read frame 7075...\n');
try
    frame = read_ffmpeg_frame(vi, 7075);
    fprintf('Successfully read frame 7075\n');
catch ME
    fprintf('Error: %s\n', ME.message);
end
