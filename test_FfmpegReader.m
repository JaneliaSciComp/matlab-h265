function test_FfmpegReader()
% TEST_FFMPEGREADER Test FfmpegReader class
%   Throws error on failure.

% This test requires test_output.mp4 from test_write
if ~isfile('test_output.mp4')
    error('test_FfmpegReader:missingFile', 'test_output.mp4 not found. Run test_write first.');
end

% Open video
vid = FfmpegReader('test_output.mp4');

% Verify properties are set
assert(~isempty(vid.filename), 'filename not set');
assert(vid.num_frames > 0, 'num_frames should be positive');
assert(vid.width > 0, 'width should be positive');
assert(vid.height > 0, 'height should be positive');
assert(vid.frame_rate() > 0, 'frame_rate should be positive');
assert(vid.duration() > 0, 'duration should be positive');

% Read single frame
frame1 = vid.read(1);
assert(isa(frame1, 'uint8'), 'Frame should be uint8');
assert(size(frame1, 1) == vid.height, 'Frame height mismatch');
assert(size(frame1, 2) == vid.width, 'Frame width mismatch');

% Read batch
frames = vid.read(1, 10);
assert(size(frames, 3) == 10, 'Batch should have 10 frames');
assert(size(frames, 1) == vid.height, 'Batch frame height mismatch');
assert(size(frames, 2) == vid.width, 'Batch frame width mismatch');

% Verify first frame of batch matches single read
assert(isequal(frame1, frames(:,:,1)), 'First batch frame should match single read');

% Destructor will clean up
clear vid;

end
