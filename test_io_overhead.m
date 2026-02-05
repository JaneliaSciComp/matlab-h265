% Test raw file I/O speed vs VideoReader overhead

% Create a 1MB test file
data = uint8(zeros(1024, 1024));
f = fopen('/tmp/test_1mb.bin', 'wb');
fwrite(f, data, 'uint8');
fclose(f);

% Time 100 random reads of 1MB (open/read/close each time)
times = zeros(100, 1);
for i = 1:100
    tic;
    f = fopen('/tmp/test_1mb.bin', 'rb');
    d = fread(f, [1024, 1024], 'uint8');
    fclose(f);
    times(i) = toc * 1000;
end
fprintf('Raw file I/O (open+read+close 1 MB): mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));

% Time with file already open
f = fopen('/tmp/test_1mb.bin', 'rb');
for i = 1:100
    tic;
    fseek(f, 0, 'bof');
    d = fread(f, [1024, 1024], 'uint8');
    times(i) = toc * 1000;
end
fclose(f);
fprintf('Raw file I/O (seek+read 1 MB):       mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));

% Now time VideoReader read() calls
vr = VideoReader('movie.avi');
times = zeros(100, 1);
rng(42);
indices = randperm(vr.NumFrames, 100);
for i = 1:100
    tic;
    frame = read(vr, indices(i));
    times(i) = toc * 1000;
end
fprintf('VideoReader read() random frames:    mean=%.2f ms, median=%.2f ms\n', mean(times), median(times));
