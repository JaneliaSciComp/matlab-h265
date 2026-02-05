files = {'movie_keyint10.mp4', 'movie_keyint50.mp4', 'movie_closed_gop.mp4'};
for i = 1:length(files)
    fprintf('Testing %s... ', files{i});
    vi = open_ffmpeg_video(files{i});
    frame = read_ffmpeg_frame(vi, 1000);
    fprintf('%d frames, frame 1000 size %dx%d\n', vi.num_frames, size(frame,1), size(frame,2));
    close_ffmpeg_video(vi);
end
fprintf('All tests passed!\n');
