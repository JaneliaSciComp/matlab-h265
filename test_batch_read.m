% Test batch frame reading vs individual reads

vi = open_ffmpeg_video('movie_keyint10.mp4');
fprintf('Opened video: %d frames, %dx%d\n\n', vi.num_frames, vi.width, vi.height);

% Test reading 100 sequential frames
start_frame = 5000;
end_frame = 5099;
num_frames = end_frame - start_frame + 1;

% Method 1: Individual reads
fprintf('Reading frames %d-%d individually...\n', start_frame, end_frame);
tic;
frames1 = zeros(vi.height, vi.width, num_frames, 'uint8');
for i = 1:num_frames
    frames1(:,:,i) = read_ffmpeg_frame(vi, start_frame + i - 1);
end
t1 = toc;
fprintf('  Individual reads: %.1f ms total, %.2f ms/frame\n', t1*1000, t1*1000/num_frames);

% Method 2: Batch read
fprintf('Reading frames %d-%d in batch...\n', start_frame, end_frame);
tic;
frames2 = read_ffmpeg_frames(vi, start_frame, end_frame);
t2 = toc;
fprintf('  Batch read: %.1f ms total, %.2f ms/frame\n', t2*1000, t2*1000/num_frames);

% Verify results match
if isequal(frames1, frames2)
    fprintf('\nResults match!\n');
else
    fprintf('\nWARNING: Results do not match!\n');
    diff_count = sum(frames1(:) ~= frames2(:));
    fprintf('  %d pixels differ\n', diff_count);
end

fprintf('\nSpeedup: %.1fx\n', t1/t2);

close_ffmpeg_video(vi);
