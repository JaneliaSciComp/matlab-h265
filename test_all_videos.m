function test_all_videos()
% TEST_ALL_VIDEOS Test reading from multiple video files
%   Throws error on failure. Skips if test files are missing.

files = {'movie_keyint10.mp4', 'movie_keyint50.mp4', 'movie_closed_gop.mp4'};

% Check which files exist
existing_files = {};
for i = 1:length(files)
    if isfile(files{i})
        existing_files{end+1} = files{i}; %#ok<AGROW>
    end
end

if isempty(existing_files)
    return;  % No test files available, skip test
end

for i = 1:length(existing_files)
    vi = open_ffmpeg_video(existing_files{i});

    assert(vi.num_frames > 0, 'num_frames should be positive for %s', existing_files{i});

    % Try to read a frame
    frame_idx = min(1000, vi.num_frames);
    frame = read_ffmpeg_frame(vi, frame_idx);

    assert(size(frame, 1) == vi.height, 'Frame height mismatch for %s', existing_files{i});
    assert(size(frame, 2) == vi.width, 'Frame width mismatch for %s', existing_files{i});

    close_ffmpeg_video(vi);
end

end
