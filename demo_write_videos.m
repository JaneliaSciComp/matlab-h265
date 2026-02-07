function demo_write_videos()
% DEMO_WRITE_VIDEOS Create sample grayscale and RGB video files
%   Writes test_gray.mp4 and test_rgb.mp4 to the current directory.

width = 256;
height = 256;
frame_count = 100;
frame_rate = 30;

% Generate grayscale frames (low-pass filtered for realistic content)
fprintf('Generating grayscale frames...\n');
raw_gray = randi([0 255], height, width, frame_count);
gray_frames = uint8(imgaussfilt(double(raw_gray), 5));

% Write grayscale video
fprintf('Writing test_gray.mp4...\n');
writer = h265.Writer('test_gray.mp4', width, height, frame_rate, 'is_gray', true);
for i = 1:frame_count
  writer.write(gray_frames(:,:,i));
end
delete(writer);
fprintf('  Written %d frames\n', frame_count);

% Generate RGB frames (low-pass filtered for realistic content)
fprintf('Generating RGB frames...\n');
raw_rgb = randi([0 255], height, width, 3, frame_count);
rgb_frames = uint8(imgaussfilt(double(raw_rgb), 5));

% Write RGB video
fprintf('Writing test_rgb.mp4...\n');
writer = h265.Writer('test_rgb.mp4', width, height, frame_rate);
for i = 1:frame_count
  writer.write(rgb_frames(:,:,:,i));
end
delete(writer);
fprintf('  Written %d frames\n', frame_count);

fprintf('Done.\n');

end
