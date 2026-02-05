function build_mex(varargin)
% BUILD_MEX Compile all MEX functions for H.265 video reading and writing
%
% Only rebuilds files where the source is newer than the MEX file.
%
% Usage:
%   build_mex          - Build out-of-date MEX files
%   build_mex --clean  - Delete all MEX files

% Parse arguments
do_clean = nargin > 0 && strcmp(varargin{1}, '--clean');

% Define all MEX targets: {source_file, mex_args...}
targets = {
    % Video reading functions
    {'open_ffmpeg_video.c', '-lavformat', '-lavcodec', '-lavutil'}
    {'read_ffmpeg_frame.c', '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
    {'read_ffmpeg_frames.c', '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
    {'close_ffmpeg_video.c', '-lavformat', '-lavcodec', '-lavutil'}
    % H.265 writing functions
    {'open_h265_write.c', '-lavformat', '-lavcodec', '-lavutil', '-lswscale'}
    {'write_h265_frame.c', '-lavformat', '-lavcodec', '-lavutil'}
    {'close_h265_write.c', '-lavformat', '-lavcodec', '-lavutil'}
};

mex_ext = mexext;

if do_clean
    % Delete all MEX files
    deleted_count = 0;
    for i = 1:numel(targets)
        src_file = targets{i}{1};
        [~, name, ~] = fileparts(src_file);
        mex_file = [name '.' mex_ext];

        if exist(mex_file, 'file')
            delete(mex_file);
            fprintf('Deleted %s\n', mex_file);
            deleted_count = deleted_count + 1;
        end
    end
    if deleted_count == 0
        fprintf('Nothing to clean.\n');
    else
        fprintf('Deleted %d file(s).\n', deleted_count);
    end
else
    % Build out-of-date MEX files
    built_count = 0;
    for i = 1:numel(targets)
        src_file = targets{i}{1};
        mex_args = targets{i}(2:end);

        [~, name, ~] = fileparts(src_file);
        mex_file = [name '.' mex_ext];

        % Check if rebuild is needed
        src_info = dir(src_file);
        mex_info = dir(mex_file);

        if isempty(src_info)
            error('build_mex:missingSource', 'Source file not found: %s', src_file);
        end

        needs_build = isempty(mex_info) || (src_info.datenum > mex_info.datenum);

        if needs_build
            fprintf('Building %s...\n', name);
            mex(src_file, mex_args{:});
            built_count = built_count + 1;
        end
    end

    if built_count == 0
        fprintf('All MEX files are up to date.\n');
    else
        fprintf('Built %d file(s).\n', built_count);
    end
end
end
